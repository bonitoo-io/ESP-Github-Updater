#ifndef ESP_GITHUB_UPDATER_H
#define ESP_GITHUB_UPDATER_H


#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <functional>

class releaseInfo {
public:
    String version;
    String tag;
    bool isPrelease = false;
    String assetUrl;
    bool assetIsBin = false;
public:
    releaseInfo() {};
    releaseInfo(const releaseInfo &other) {
        *this = other;
    }
    releaseInfo &operator=(const releaseInfo &other) {
        version = other.version;
        tag = other.tag;
        isPrelease = other.isPrelease;
        assetIsBin = other.assetIsBin;
        assetUrl = other.assetUrl;
        return *this;
    }

};

typedef std::function<void(HTTPClient &res)> GithubResponseHandler;
typedef std::function<void(int percent)> UpdateProgressHandler;

class ESPGithubUpdater {
public:
    ESPGithubUpdater(String owner, String repoName);
    ESPGithubUpdater(String owner, String repoName, String user, String token);
    virtual ~ESPGithubUpdater();
    String getLatestVersion(bool includePrerelease = false);
    bool checkVersion(String version);
    bool runUpdate(String version, UpdateProgressHandler handler = nullptr);
    String getLastError() const { return _lastError; }
private:
    bool fetchVersion(String version, bool includePrelease = false);
    String buildGithubPath(String version, bool includePrelease = false);
    bool githubAPICall(String &path, GithubResponseHandler handler = nullptr);    
private:
    String _owner;
    String _repoName;
    String _user;
    String _token;
    String _lastError;
    releaseInfo _cache;
    BearSSL::WiFiClientSecure *_client = nullptr;
    BearSSL::X509List  *_cert = nullptr;
    UpdateProgressHandler _update; 
};


#endif //ESP_GITHUB_UPDATER_H