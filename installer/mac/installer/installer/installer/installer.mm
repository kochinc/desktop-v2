#import "installer.hpp"
#import "../Logger.h"
#include "installhelper_mac.h"
#include "processes_helper.h"

@interface Installer()

-(void)execution;

@end

@implementation Installer


-(id)init
{
    if (self = [super init])
    {
        d_group_ = nil;
    }
    return self;
}

-(id)initWithPath: (NSString *)path
{
    if (self = [super initWithUpdatePath: path])
    {
        d_group_ = nil;
    }
    return self;
}

- (void)setObjectForCallback: (SEL)aSelector withObject:(id)arg
{
    callbackSelector_ = aSelector;
    callbackObject_ = arg;
}

- (BOOL)isFolderAlreadyExist
{
    if (isUseUpdatePath_)
    {
        return NO;
    }
    else
    {
        NSFileManager *manager = [NSFileManager defaultManager];
        return [manager fileExistsAtPath:[self getFullInstallPath] isDirectory:nil];
    }
}

-(void)start: (BOOL)keepBoth
{
    [[Logger sharedLogger] logAndStdOut:@"Installer starting"];

    isKeepBoth_ = keepBoth;
    isCanceled_ = false;
    
    d_group_ = dispatch_group_create();
    dispatch_queue_t bg_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);

    dispatch_group_async(d_group_, bg_queue, ^{
        [self execution];
    });
}

// cancel and do uninstall
-(void)cancel
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        isCanceled_ = true;
        self.currentState = STATE_CANCELED;
    }
    [self waitForCompletion];
    
    [[NSFileManager defaultManager] removeItemAtPath:[self getFullInstallPath] error:nil];
}

- (void)appDidLaunch:(NSNotification*)note
{
    self.progress = 100;
    self.currentState = STATE_LAUNCHED;
    [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
}

-(void)runLauncher
{
    [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:self selector:@selector(appDidLaunch:) name:NSWorkspaceDidLaunchApplicationNotification object:nil];
    [[NSWorkspace sharedWorkspace] launchApplication: [self getFullInstallPath]];
}

-(void) execution
{
    int prevOverallProgress = 0;
    self.progress = 0;
    self.currentState = STATE_EXTRACTING;
    [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
    
    // connect to helper
    NSDate *waitingHelperSince_ = [NSDate date];
    while (!helper_.connect())
    {
        usleep(10000); // 10 milliseconds
        int seconds = -(int)[waitingHelperSince_ timeIntervalSinceNow];
        if (seconds > 5)
        {
            NSString *errStr = @"Couldn't connect to helper in time";
            [[Logger sharedLogger] logAndStdOut:errStr];
            self.lastError = errStr;
            self.currentState = STATE_ERROR;
            [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
            return;
        }
    }
    
    // kill processes with "Windscribe" name
    ProcessesHelper processesHelper;
    std::vector<pid_t> processesList = processesHelper.getPidsByProcessname("Windscribe");
    if (processesList.size() > 0)
    {
        [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"Waiting for Windscribe programs to close..."]];
        
        for (auto pid : processesList)
        {
            helper_.killProcess(pid);
        }
        
        // get WindscribeEngine processes
        std::vector<pid_t> engineList = processesHelper.getPidsByProcessname("WindscribeEngine");
        processesList.insert(processesList.end(), engineList.begin(), engineList.end());
        
        // wait for finish (maximum 10 sec)
        NSDate *waitingSince_ = [NSDate date];
        while (true)
        {
            bool bAllFinished = true;
            for (auto pid : processesList)
            {
                if (!processesHelper.isProcessFinished(pid))
                {
                    bAllFinished = false;
                    break;
                }
            }
            
            if (bAllFinished)
            {
                break;
            }
            
            usleep(10000); // 10 milliseconds
            int seconds = -(int)[waitingSince_ timeIntervalSinceNow];
            if (seconds > 10)
            {
                NSString *errStr = @"Couldn't kill running Windscribe programs in time. Please close running Windscribe programs manually and try install again.";
                [[Logger sharedLogger] logAndStdOut:errStr];
                self.lastError = errStr;
                self.currentState = STATE_ERROR;
                [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
                return;
            }
        }
    }
    if (processesList.size() > 0)
    {
        [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"All Windscribe programs closed"]];
    }

    // remove previously existing application
    if ([self isFolderAlreadyExist] || isUseUpdatePath_)
    {
        [[Logger sharedLogger] logAndStdOut:@"Windscribe exists in desired folder"];

        if (isKeepBoth_)
        {
            for (int i = 2; ; i++)
            {
                pathInd_ = i;
                if ([self isFolderAlreadyExist] == NO)
                {
                    [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"Keeping existing app and installing new one as : %i", i]];
                    break;
                }
            }
        }
        else
        {
            [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"Attempting to remove: %@", [self getFullInstallPath]]];
            
            NSString *command = [NSString stringWithFormat:@"/bin/rm -r '%@'", [self getFullInstallPath]];
            std::string strCommand = std::string([command UTF8String]);
            bool success;
            helper_.executeRootCommand(strCommand, success);
            if (!success)
            {
                NSString *errStr = @"Previous version of the program cannot be deleted. Please contact support.";
                [[Logger sharedLogger] logAndStdOut:errStr];
                self.lastError = errStr;
                self.currentState = STATE_ERROR;
                [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
                helper_.stop();
                return;
            }
            else
            {
                [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"Removed!"]];
            }
        }
    }
    
    // remove helper from version 1
    bool bTemp;
    helper_.executeRootCommand("launchctl unload /Library/LaunchDaemons/com.aaa.windscribe.OVPNHelper.plist", bTemp);
    helper_.executeRootCommand("rm /Library/LaunchDaemons/com.aaa.windscribe.OVPNHelper.plist", bTemp);
    helper_.executeRootCommand("rm /Library/PrivilegedHelperTools/com.aaa.windscribe.OVPNHelper", bTemp);
    
    [[Logger sharedLogger] logAndStdOut:@"Writing blocks"];
    
    uid_t userId = getuid();
    gid_t groupId = getgid();
    
    NSString* archivePathFromApp = [[NSBundle mainBundle] pathForResource:@"windscribe.7z" ofType:nil];
    std::wstring strArchivePath = NSStringToStringW(archivePathFromApp);
    std::wstring strPath = NSStringToStringW([self getFullInstallPath]);
    
    if (!helper_.setPaths(strArchivePath, strPath, userId, groupId))
    {
        NSString *errStr = @"setPaths in helper failed";
        [[Logger sharedLogger] logAndStdOut:errStr];
        self.lastError = errStr;
        self.currentState = STATE_ERROR;
        [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
        helper_.stop();
        return;
    }

    while (true)
    {
        //[NSThread sleepForTimeInterval:0.2];
        std::lock_guard<std::mutex> lock(mutex_);
        if (isCanceled_)
        {
            [[Logger sharedLogger] logAndStdOut:@"Block install cancelled"];

            self.progress = 0;
            self.currentState = STATE_CANCELED;
            helper_.stop();
            return;
        }

        int progressOfBlock = helper_.executeFilesStep();
            
        // block is finished?
        if (progressOfBlock >= 100)
        {
            [[Logger sharedLogger] logAndStdOut:@"Block install 100+"];

            self.progress = prevOverallProgress + (int)(100.0);
            [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
            break;
        }
        // error from block?
        else if (progressOfBlock < 0)
        {
            [[Logger sharedLogger] logAndStdOut:[NSString stringWithFormat:@"Block < 0: %i", progressOfBlock]];
            self.lastError = @"Extracting error";
            self.currentState = STATE_ERROR;
            [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
            helper_.stop();
            return;
        }
        else
        {
            // [[Logger sharedLogger] writeToLog:@"Block processing"];
            self.progress = prevOverallProgress + (int)(progressOfBlock);
            [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
        }
    }

    [[Logger sharedLogger] logAndStdOut:@"Done writing blocks"];
    
    // create symlink for cli
    [[Logger sharedLogger] logAndStdOut:@"Creating CLI symlink"];
    NSString *filepath = [NSString stringWithFormat:@"%@%@", [self getFullInstallPath], @"/Contents/MacOS/windscribe-cli"];
    NSString *sympath = @"/usr/local/bin/windscribe-cli";
    [[NSFileManager defaultManager] removeItemAtPath:sympath error:nil];
    [[NSFileManager defaultManager] createSymbolicLinkAtPath:sympath withDestinationPath:filepath error:nil];

    self.progress = 100;
    self.currentState = STATE_FINISHED;
    [callbackObject_ performSelectorOnMainThread:callbackSelector_ withObject:self waitUntilDone:NO];
    
    helper_.stop();
}

-(void)waitForCompletion
{
    if (d_group_ != nil)
    {
        dispatch_group_wait(d_group_, DISPATCH_TIME_FOREVER);
        d_group_ = nil;
    }
}

std::wstring NSStringToStringW ( NSString* Str )
{
    NSStringEncoding pEncode    =   CFStringConvertEncodingToNSStringEncoding ( kCFStringEncodingUTF32LE );
    NSData* pSData              =   [ Str dataUsingEncoding : pEncode ];
   
    return std::wstring ( (wchar_t*) [ pSData bytes ], [ pSData length] / sizeof ( wchar_t ) );
}

NSString* StringWToNSString ( const std::wstring& Str )
{
    NSString* pString = [ [ NSString alloc ]
                        initWithBytes : (char*)Str.data()
                               length : Str.size() * sizeof(wchar_t)
                             encoding : CFStringConvertEncodingToNSStringEncoding ( kCFStringEncodingUTF32LE ) ];
    return pString;
}

@end




