#include "accountwindowitem.h"

#include <QPainter>
#include <QDesktopServices>
#include <QUrl>
#include <QTextDocument>
#include "utils/hardcodedsettings.h"
#include "graphicresources/fontmanager.h"
#include "languagecontroller.h"
#include "dpiscalemanager.h"

namespace PreferencesWindow {

AccountWindowItem::AccountWindowItem(ScalableGraphicsObject *parent, AccountInfo *accountInfo) : BasePage(parent)
{
    setFlag(QGraphicsItem::ItemIsFocusable);

    connect(accountInfo, &AccountInfo::usernameChanged, this, &AccountWindowItem::onUsernameChanged);
    connect(accountInfo, &AccountInfo::emailChanged, this, &AccountWindowItem::onEmailChanged);
    connect(accountInfo, &AccountInfo::isNeedConfirmEmailChanged, this, &AccountWindowItem::onIsNeedConfirmEmailChanged);
    connect(accountInfo, &AccountInfo::planChanged, this, &AccountWindowItem::onPlanChanged);
    connect(accountInfo, &AccountInfo::expireDateChanged, this, &AccountWindowItem::onExpireDateChanged);
    connect(accountInfo, &AccountInfo::authHashChanged, this, &AccountWindowItem::onAuthHashChanged);

    usernameItem_ = new UsernameItem(this);
    usernameItem_->setUsername(accountInfo->username());
    addItem(usernameItem_);

    emailItem_ = new EmailItem(this);
    emailItem_->setEmail(accountInfo->email());
    emailItem_->setNeedConfirmEmail(accountInfo->isNeedConfirmEmail());

    connect(emailItem_, &EmailItem::emptyEmailButtonClick, this,
        [this]{ QDesktopServices::openUrl(QUrl(QString("https://%1/myaccount?app_session=%2").arg(HardcodedSettings::instance().serverUrl(), authHash_))); });
    connect(emailItem_, &EmailItem::sendEmailClick, this, &AccountWindowItem::sendConfirmEmailClick);
    addItem(emailItem_);

    planItem_ = new PlanItem(this);
    connect(planItem_, &PlanItem::upgradeClicked, this, &AccountWindowItem::onUpgradeClicked);
    planItem_->setPlan(accountInfo->plan());
    addItem(planItem_);

    expireDateItem_ = new ExpireDateItem(this);
    expireDateItem_->setDate(accountInfo->expireDate());
    addItem(expireDateItem_);

    authHash_ = accountInfo->authHash();

    editAccountItem_ = new OpenUrlItem(this);
    editAccountItem_->setText(tr("Edit Account Details"));
    editAccountItem_->setUrl([this]{ return QString("https://%1/myaccount?app_session=%2").arg(HardcodedSettings::instance().serverUrl(), authHash_); });
    addItem(editAccountItem_);

    textItem_ = new QGraphicsTextItem(this);
    textItem_->setPlainText(tr("Login to view your account info"));
    textItem_->setFont(*FontManager::instance().getFont(14, false));
    textItem_->setDefaultTextColor(Qt::white);
    textItem_->setTextWidth(125);
    textItem_->document()->setDefaultTextOption(QTextOption(Qt::AlignHCenter));

    loginButton_ = new CommonGraphics::BubbleButtonDark(this, 69, 24, 12, 20);
    loginButton_->setText(tr("Login"));
    loginButton_->setFont(FontDescr(12,false));
    connect(loginButton_, &CommonGraphics::BubbleButtonDark::clicked, this, &AccountWindowItem::noAccountLoginClick);

    updateWidgetPos();

    connect(&LanguageController::instance(), &LanguageController::languageChanged, this, &AccountWindowItem::onLanguageChanged);

}

QString AccountWindowItem::caption()
{
    return QT_TRANSLATE_NOOP("PreferencesWindow::PreferencesWindowItem", "Account");
}

void AccountWindowItem::setLoggedIn(bool loggedIn)
{
    usernameItem_->setVisible(loggedIn);
    emailItem_->setVisible(loggedIn);
    planItem_->setVisible(loggedIn);
    expireDateItem_->setVisible(loggedIn);
    editAccountItem_->setVisible(loggedIn);

    textItem_->setVisible(!loggedIn);
    loginButton_->setVisible(!loggedIn);
}

void AccountWindowItem::setConfirmEmailResult(bool bSuccess)
{
    emailItem_->setConfirmEmailResult(bSuccess);
}

void AccountWindowItem::updateScaling()
{
    BasePage::updateScaling();
    textItem_->setFont(*FontManager::instance().getFont(14, false));
    textItem_->setTextWidth(125*G_SCALE);
    updateWidgetPos();
}

void AccountWindowItem::onUsernameChanged(const QString &username)
{
    usernameItem_->setUsername(username);
}

void AccountWindowItem::onEmailChanged(const QString &email)
{
    emailItem_->setEmail(email);
}

void AccountWindowItem::onIsNeedConfirmEmailChanged(bool bNeedConfirm)
{
    emailItem_->setNeedConfirmEmail(bNeedConfirm);
}

void AccountWindowItem::onPlanChanged(qint64 plan)
{
    planItem_->setPlan(plan);
}

void AccountWindowItem::onExpireDateChanged(const QString &date)
{
    expireDateItem_->setDate(date);
}

void AccountWindowItem::onAuthHashChanged(const QString &authHash)
{
    authHash_ = authHash;
}

void AccountWindowItem::onUpgradeClicked()
{
    QDesktopServices::openUrl(QUrl( QString("https://%1/upgrade?pcpid=desktop_upgrade").arg(HardcodedSettings::instance().serverUrl())));
}

void AccountWindowItem::onLanguageChanged()
{
    loginButton_->setText(tr("Login"));
    textItem_->setPlainText(tr("Login to view your account info"));

    updateWidgetPos();
}

void AccountWindowItem::updateWidgetPos()
{
    int textCenterX = static_cast<int>(boundingRect().width()/2 - textItem_->boundingRect().width()/2);
    textItem_->setPos(textCenterX, 85 * G_SCALE);

    int loginCenterX = static_cast<int>(boundingRect().width()/2 - loginButton_->boundingRect().width()/2);
    loginButton_->setPos(loginCenterX, 155 * G_SCALE);
}

} // namespace PreferencesWindow
