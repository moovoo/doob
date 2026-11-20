#ifndef SPOTIFYSETTINGSPAGE_H
#define SPOTIFYSETTINGSPAGE_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class SpotifyAuth;
class SpotifyProvider;

// The "Spotify" settings topic page (the preferences dialog's "Plugins"
// group): the sign-in state and its controls (Sign in — the PKCE loopback
// flow, doubling as re-login — and Sign out), and the "use your own app"
// fields (the client id and the loopback port, written to the plugin's
// settings — a client-id change forces a re-login, the token belongs to
// the app that issued it).
class SpotifySettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SpotifySettingsPage(SpotifyProvider *provider,
                                 QWidget *parent = nullptr);

private slots:
    void onSignIn();
    void onSignOut();
    void onSave();

private:
    void updateStatus();
    // The signed-in user's display name (a lazy Web API fetch).
    void updateProfile();

    SpotifyProvider* m_provider;
    QLabel* m_status;
    QPushButton* m_signIn;
    QPushButton* m_signOut;
    QLineEdit* m_clientId;
    QSpinBox* m_port;
    QString m_profileName;
};

#endif // SPOTIFYSETTINGSPAGE_H
