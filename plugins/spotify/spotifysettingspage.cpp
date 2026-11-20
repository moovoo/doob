#include "spotifysettingspage.h"

#include "spotifyauth.h"
#include "spotifyprovider.h"
#include "spotifywebapi.h"

#include <doob/settingsmanager.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SpotifySettingsPage::SpotifySettingsPage(SpotifyProvider *provider,
                                         QWidget *parent)
    : QWidget(parent)
    , m_provider(provider)
{
    SettingsManager* settings = SettingsManager::getInstance();
    SpotifyAuth* auth = m_provider->auth();

    QVBoxLayout* layout = new QVBoxLayout(this);

    // The account state and its controls.
    layout->addWidget(new QLabel("Spotify account", this));

    m_status = new QLabel(this);
    m_status->setObjectName("statusLabel");
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    QHBoxLayout* accountButtons = new QHBoxLayout();
    m_signIn = new QPushButton("Sign in", this);
    m_signIn->setObjectName("signInButton");
    connect(m_signIn, &QPushButton::clicked, this,
            &SpotifySettingsPage::onSignIn);
    m_signOut = new QPushButton("Sign out", this);
    m_signOut->setObjectName("signOutButton");
    connect(m_signOut, &QPushButton::clicked, this,
            &SpotifySettingsPage::onSignOut);
    accountButtons->addWidget(m_signIn);
    accountButtons->addWidget(m_signOut);
    accountButtons->addStretch(1);
    layout->addLayout(accountButtons);

    // The user's own app: the client id (a public identifier — the
    // default is the built-in one) and the loopback port (it must match
    // the redirect URI the app registered).
    layout->addWidget(new QLabel("Use your own app", this));

    QHBoxLayout* clientIdRow = new QHBoxLayout();
    clientIdRow->addWidget(new QLabel("Client ID", this));
    m_clientId = new QLineEdit(this);
    m_clientId->setObjectName("clientIdEdit");
    m_clientId->setText(settings->value(kSpotifyClientIdKey).toString());
    clientIdRow->addWidget(m_clientId);
    layout->addLayout(clientIdRow);

    QHBoxLayout* portRow = new QHBoxLayout();
    portRow->addWidget(new QLabel("Loopback port", this));
    m_port = new QSpinBox(this);
    m_port->setObjectName("redirectPortSpin");
    m_port->setRange(1, 65535);
    m_port->setValue(auth->redirectPort());
    portRow->addWidget(m_port);
    portRow->addStretch(1);
    layout->addLayout(portRow);

    QLabel* hint = new QLabel(
        "The redirect URI http://127.0.0.1:<port>/callback must be "
        "registered in the app (Spotify Developer Dashboard). Changing "
        "the client id signs the account out.",
        this);
    hint->setObjectName("clientHint");
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QHBoxLayout* saveRow = new QHBoxLayout();
    QPushButton* save = new QPushButton("Save", this);
    save->setObjectName("saveButton");
    connect(save, &QPushButton::clicked, this,
            &SpotifySettingsPage::onSave);
    saveRow->addWidget(save);
    saveRow->addStretch(1);
    layout->addLayout(saveRow);

    layout->addStretch(1);

    connect(auth, &SpotifyAuth::authChanged, this,
            &SpotifySettingsPage::updateStatus);
    connect(auth, &SpotifyAuth::statusMessage, this,
            [this](const QString &message) {
                // The state itself carries the "acquired" line (the
                // sign-in update wins over the transient progress).
                if (message.startsWith(QStringLiteral("token acquired")))
                    return;
                m_status->setText(message);
            });
    updateStatus();
}

void SpotifySettingsPage::updateStatus()
{
    SpotifyAuth* auth = m_provider->auth();
    if (auth->state() == SpotifyAuth::State::NotSignedIn) {
        m_status->setText("Not signed in.");
        m_signOut->setEnabled(false);
        return;
    }
    m_signOut->setEnabled(true);
    const SpotifyAuth::Token token = auth->token();
    QString text = m_profileName.isEmpty()
                       ? QStringLiteral("Signed in.")
                       : QStringLiteral("Signed in as %1.").arg(m_profileName);
    text += QStringLiteral(" Token valid until %1.")
                .arg(token.expiresAt.toString("yyyy-MM-dd HH:mm"));
    m_status->setText(text);
    updateProfile();
}

void SpotifySettingsPage::updateProfile()
{
    SpotifyAuth* auth = m_provider->auth();
    if (auth->state() != SpotifyAuth::State::SignedIn || !m_profileName.isEmpty())
        return;
    auth->validToken(
        [this](const SpotifyAuth::Token &token, const QString &err) {
            if (!err.isEmpty())
                return;
            m_provider->api()->setAccessToken(token.accessToken);
            m_provider->api()->fetchCurrentUser(
                [this](const QJsonObject &obj, const QString &) {
                    m_profileName = obj.value("display_name")
                                        .toString(obj.value("id").toString());
                    updateStatus();
                });
        });
}

void SpotifySettingsPage::onSignIn()
{
    m_provider->auth()->login([this](const SpotifyAuth::Token &,
                                     const QString &err) {
        if (!err.isEmpty())
            m_status->setText(err);
        // On success the authChanged → updateStatus already ran.
    });
}

void SpotifySettingsPage::onSignOut()
{
    m_provider->auth()->logout();
    m_profileName.clear();
    updateStatus();
}

void SpotifySettingsPage::onSave()
{
    SettingsManager* settings = SettingsManager::getInstance();
    settings->setValue(kSpotifyClientIdKey, m_clientId->text().trimmed());
    settings->setValue(kSpotifyRedirectPortKey, m_port->value());
}
