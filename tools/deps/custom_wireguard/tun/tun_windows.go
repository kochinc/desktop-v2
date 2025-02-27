/* SPDX-License-Identifier: MIT
 *
 * Copyright (C) 2018-2020 WireGuard LLC. All Rights Reserved.
 */

package tun

import (
	"errors"
	"fmt"
	"os"
	"sync/atomic"
	"time"
	_ "unsafe"
	"log"
	"unsafe"

	"golang.org/x/sys/windows"
	"golang.org/x/crypto/blake2s"

	"golang.zx2c4.com/wireguard/tun/wintun"
)

const (
	rateMeasurementGranularity = uint64((time.Second / 2) / time.Nanosecond)
	spinloopRateThreshold      = 800000000 / 8                                   // 800mbps
	spinloopDuration           = uint64(time.Millisecond / 80 / time.Nanosecond) // ~1gbit/s
)

type rateJuggler struct {
	current       uint64
	nextByteCount uint64
	nextStartTime int64
	changing      int32
}

type NativeTun struct {
	wt        *wintun.Adapter
	handle    windows.Handle
	close     bool
	events    chan Event
	errors    chan error
	forcedMTU int
	rate      rateJuggler
	session   wintun.Session
	readWait  windows.Handle
}

var WintunPool *wintun.Pool

func init() {
	var err error
	WintunPool, err = wintun.MakePool("Windscribe WireGuard")
	if err != nil {
		panic(fmt.Errorf("Failed to make pool: %w", err))
	}
}

//go:linkname procyield runtime.procyield
func procyield(cycles uint32)

//go:linkname nanotime runtime.nanotime
func nanotime() int64

//
// Windscribe additions.
// Generate a fixed GUID to reuse an existing profile.
//
const deterministicGUIDLabel = "Deterministic Windscribe WireGuard Windows GUID v1"
func GenerateDeterministicGUID(ifname string) *windows.GUID {
	b2, _ := blake2s.New256(nil)
	b2.Write([]byte(deterministicGUIDLabel))
	b2.Write([]byte(ifname))
	return (*windows.GUID)(unsafe.Pointer(&b2.Sum(nil)[0]))
}

//
// CreateTUN creates a Wintun interface with the given name. Should a Wintun
// interface with the same name exist, it is reused.
//
func CreateTUN(ifname string, mtu int) (Device, error) {
	// Try to create an interface with a deterministic GUID, thus reusing an existing profile.
	tun, err := CreateTUNWithRequestedGUID(ifname, GenerateDeterministicGUID(ifname), mtu)
	if tun != nil {
		return tun, err
	}
	// If it failed (though it should not), then don't fail, but instead create an interface with
	// a random GUID (the default behaviour).
	return CreateTUNWithRequestedGUID(ifname, nil, mtu)
}

//
// CreateTUNWithRequestedGUID creates a Wintun interface with the given name and
// a requested GUID. Should a Wintun interface with the same name exist, it is reused.
//
func CreateTUNWithRequestedGUID(ifname string, requestedGUID *windows.GUID, mtu int) (Device, error) {
	var err error
	var wt *wintun.Adapter

	// Does an interface with this name already exist?
	log.Println("CreateTUNWithRequestedGUID:", ifname, " guid =", requestedGUID)
	wt, err = WintunPool.OpenAdapter(ifname)
	if err == nil {
		// If so, we delete it, in case it has weird residual configuration.
		_, err = wt.Delete(true)
		if err != nil {
			return nil, fmt.Errorf("Error deleting already existing interface: %w", err)
		}
	}
	wt, _, err = WintunPool.CreateAdapter(ifname, requestedGUID)
	if err != nil {
		return nil, fmt.Errorf("Error creating interface: %w", err)
	}

	forcedMTU := 1420
	if mtu > 0 {
		forcedMTU = mtu
	}

	tun := &NativeTun{
		wt:        wt,
		handle:    windows.InvalidHandle,
		events:    make(chan Event, 10),
		errors:    make(chan error, 1),
		forcedMTU: forcedMTU,
	}

	tun.session, err = wt.StartSession(0x800000) // Ring capacity, 8 MiB
	if err != nil {
		_, err = tun.wt.Delete(false)
		close(tun.events)
		return nil, fmt.Errorf("Error starting session: %w", err)
	}
	tun.readWait = tun.session.ReadWaitEvent()
	return tun, nil
}

func (tun *NativeTun) Name() (string, error) {
	return tun.wt.Name()
}

func (tun *NativeTun) File() *os.File {
	return nil
}

func (tun *NativeTun) Events() chan Event {
	return tun.events
}

func (tun *NativeTun) Close() error {
	tun.close = true
	tun.session.End()
	var err error
	if tun.wt != nil {
		_, err = tun.wt.Delete(false)
	}
	close(tun.events)
	return err
}

func (tun *NativeTun) MTU() (int, error) {
	return tun.forcedMTU, nil
}

// TODO: This is a temporary hack. We really need to be monitoring the interface in real time and adapting to MTU changes.
func (tun *NativeTun) ForceMTU(mtu int) {
	tun.forcedMTU = mtu
}

// Note: Read() and Write() assume the caller comes only from a single thread; there's no locking.

func (tun *NativeTun) Read(buff []byte, offset int) (int, error) {
retry:
	select {
	case err := <-tun.errors:
		return 0, err
	default:
	}
	start := nanotime()
	shouldSpin := atomic.LoadUint64(&tun.rate.current) >= spinloopRateThreshold && uint64(start-atomic.LoadInt64(&tun.rate.nextStartTime)) <= rateMeasurementGranularity*2
	for {
		if tun.close {
			return 0, os.ErrClosed
		}
		packet, err := tun.session.ReceivePacket()
		switch err {
		case nil:
			packetSize := len(packet)
			copy(buff[offset:], packet)
			tun.session.ReleaseReceivePacket(packet)
			tun.rate.update(uint64(packetSize))
			return packetSize, nil
		case windows.ERROR_NO_MORE_ITEMS:
			if !shouldSpin || uint64(nanotime()-start) >= spinloopDuration {
				windows.WaitForSingleObject(tun.readWait, windows.INFINITE)
				goto retry
			}
			procyield(1)
			continue
		case windows.ERROR_HANDLE_EOF:
			return 0, os.ErrClosed
		case windows.ERROR_INVALID_DATA:
			return 0, errors.New("Send ring corrupt")
		}
		return 0, fmt.Errorf("Read failed: %w", err)
	}
}

func (tun *NativeTun) Flush() error {
	return nil
}

func (tun *NativeTun) Write(buff []byte, offset int) (int, error) {
	if tun.close {
		return 0, os.ErrClosed
	}

	packetSize := len(buff) - offset
	tun.rate.update(uint64(packetSize))

	packet, err := tun.session.AllocateSendPacket(packetSize)
	if err == nil {
		copy(packet, buff[offset:])
		tun.session.SendPacket(packet)
		return packetSize, nil
	}
	switch err {
	case windows.ERROR_HANDLE_EOF:
		return 0, os.ErrClosed
	case windows.ERROR_BUFFER_OVERFLOW:
		return 0, nil // Dropping when ring is full.
	}
	return 0, fmt.Errorf("Write failed: %w", err)
}

// LUID returns Windows interface instance ID.
func (tun *NativeTun) LUID() uint64 {
	return tun.wt.LUID()
}

// RunningVersion returns the running version of the Wintun driver.
func (tun *NativeTun) RunningVersion() (version uint32, err error) {
	return wintun.RunningVersion()
}

func (rate *rateJuggler) update(packetLen uint64) {
	now := nanotime()
	total := atomic.AddUint64(&rate.nextByteCount, packetLen)
	period := uint64(now - atomic.LoadInt64(&rate.nextStartTime))
	if period >= rateMeasurementGranularity {
		if !atomic.CompareAndSwapInt32(&rate.changing, 0, 1) {
			return
		}
		atomic.StoreInt64(&rate.nextStartTime, now)
		atomic.StoreUint64(&rate.current, total*uint64(time.Second/time.Nanosecond)/period)
		atomic.StoreUint64(&rate.nextByteCount, 0)
		atomic.StoreInt32(&rate.changing, 0)
	}
}
