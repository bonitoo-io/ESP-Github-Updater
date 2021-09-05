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
  bool isPrelease;
  String binFileURL;
  String md5FileURL;
 public:
  releaseInfo() {}
  releaseInfo(const releaseInfo &other) {
    *this = other;
  }
  releaseInfo &operator=(const releaseInfo &other) {
    version = other.version;
    tag = other.tag;
    isPrelease = other.isPrelease;
    binFileURL = other.binFileURL;
    md5FileURL = other.md5FileURL;
    return *this;
  }
};

typedef std::function<void(HTTPClient &res)> GithubResponseHandler;
typedef std::function<void(int percent)> UpdateProgressHandler;

class ESPGithubUpdater {
 public:
  ESPGithubUpdater(String owner, String repoName, String fileName);
  virtual ~ESPGithubUpdater();
  void setAuthorization(String user, String token);
  void setMD5FileName(String md5FileName) { _md5File = md5FileName; }
  String getLatestVersion(bool includePrerelease = false);
  bool checkVersion(String version);
  void setInsecure() { _insecure = true; }
  bool runUpdate(String version, UpdateProgressHandler handler = nullptr);
  String getLastError() const { return _lastError; }
  void setRestartOnUpdate(bool restart) { _restartOnUpdate = restart; }
 private:
  bool fetchVersion(String version, bool includePrelease = false);
  String buildGithubPath(String version, bool includePrelease = false);
  bool githubAPICall(String &path, GithubResponseHandler handler = nullptr);    
  int getMD5Sum(const String &url, String &md5);
private:
  String _owner;
  String _repoName;
  String _fileName;
  String _user;
  String _token;
  String _md5File;
  String _lastError;
  bool _insecure = false;
  releaseInfo _cache;
  BearSSL::WiFiClientSecure *_client = nullptr;
  BearSSL::X509List  *_cert = nullptr;
  UpdateProgressHandler _update = nullptr; 
  bool _restartOnUpdate = true;
};


#endif //ESP_GITHUB_UPDATER_H