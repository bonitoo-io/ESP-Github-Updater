#include "ESPGIthubUpdater.h"
#include "HTTPJsonParser.h"
#include <JsonStreamingParser.h>
#include <ESP8266httpUpdate.h>


static const char *GithubAPI = "api.github.com"; 
static const char *GithubRepoPath = "/repos/%s/%s/releases"; 
static const char *GithubTag = "/tags/%s";
static const char *GithubLastRelease = "?per_page=1&page=1";
static const char *GithubLatestRelease = "/latest";



class ReleaseParser: public JsonListener {
private:
    String _key;
    bool _inAssests = false;
    releaseInfo *_release = nullptr;
public:
    ReleaseParser() {}
    ~ReleaseParser() { delete _release; }
    virtual void whitespace(char c) override {};
    virtual void startDocument() override {};
    virtual void key(String key) override;
    virtual void value(String value) override;
    virtual void endArray();
    virtual void endObject() override {};
    virtual void startArray() override {};
    virtual void startObject()  override;
    virtual void endDocument()  override {};

    releaseInfo *getRelease() const { return _release; }
};

void ReleaseParser::startObject() {
    if(!_inAssests) {
        if(!_release) {
            _release = new releaseInfo; 
        }
    }
}

void ReleaseParser::key(String key) {
    if(!_inAssests && key == F("assets")) {
        _inAssests = true;
    }
    _key = key;
};

void ReleaseParser:: endArray() {
    if(_inAssests) {
        _inAssests  = false;
    }
}

void ReleaseParser::value(String value) {
    //Serial.println("key: " + _key + " value: " + value);
    if(!_inAssests) {
        if ( _key == F("name")) {
            _release->version = value;
        } else if ( _key == F("tag_name")) {
            _release->version = value;
        }
    } else {
        if ( _key == F("browser_download_url")) {
            _release->assetUrl = value;
        } else if(_key == F("content_type")) {
            _release->assetIsBin = value == F("application/octet-stream");
        } 
    }
}

ESPGithubUpdater::ESPGithubUpdater(String owner, String repoName):ESPGithubUpdater(owner, repoName, "", "") {

}
ESPGithubUpdater::ESPGithubUpdater(String owner, String repoName, String user, String token):
    _owner(owner), _repoName(repoName), _user(user),_token(token) {
     
}

String ESPGithubUpdater::getLatestVersion(bool includePrerelease) {
    if (fetchVersion("", includePrerelease)) {
        return _cache.version;
    }
    return "";
}

bool ESPGithubUpdater::checkVersion(String version) {
    String path = buildGithubPath(version);
    return githubAPICall(path);
}

bool ESPGithubUpdater::fetchVersion(String version, bool includePrelease) {
    if(_cache.version.length() > 0 && version.length() > 0 && _cache.version == version) {
        return true;
    }
    String path = buildGithubPath(version, includePrelease);
    return githubAPICall(path, [this](HTTPClient &client){
        ReleaseParser rel;
        HTTPJsonParser parser(&rel);
        client.writeToStream(&parser);
        if(rel.getRelease()) {
            _cache = *rel.getRelease();
        } else {
            _cache.version = "";
            _lastError = F("no version found");
        }
    });
}

String ESPGithubUpdater::buildGithubPath(String version, bool includePrelease) {
    String path;
    // reserve maximum possible length 
    int length = strlen(GithubRepoPath) + _owner.length()+_repoName.length() + version.length() + strlen(GithubLastRelease) +1;
    char *buff = new char[length];
    sprintf(buff, GithubRepoPath, _owner.c_str(), _repoName.c_str());
    if(version.length() > 0) {
         sprintf(buff+strlen(buff), GithubTag, version.c_str());
    } else {
        if(includePrelease) {
            sprintf(buff+strlen(buff),GithubLastRelease);
        } else {
            sprintf(buff+strlen(buff),GithubLatestRelease);
        }
    }
    path = buff;
    return path;
}

bool ESPGithubUpdater::runUpdate(String version, UpdateProgressHandler handler) {
    if(!fetchVersion(version)) {
        return false;
    }
    Serial.printf_P(PSTR("Update %s, bin: %s\n"), _cache.assetUrl.c_str(),_cache.assetIsBin?F("true"):F("false"));
    if(!_cache.assetIsBin) {
        _lastError = F("Not a binary asset: ");
        _lastError += _cache.assetUrl.c_str();
        return false;
    }
      
    if(_user.length() && _token.length()) {
        ESPhttpUpdate.setAuthorization(_user.c_str(),_token.c_str());
    }
    if(handler) {
        ESPhttpUpdate.onProgress([handler](size_t act, size_t total) {
            float prog = act;
            prog = (prog/total)*100;
            handler((int)prog);
        });
    }
    
    ESPhttpUpdate.followRedirects(true);
    ESPhttpUpdate.closeConnectionsOnUpdate(false);
    ESPhttpUpdate.rebootOnUpdate(_restartOnUpdate);
    t_httpUpdate_return ret = ESPhttpUpdate.update(*_client, _cache.assetUrl);
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            _lastError = ESPhttpUpdate.getLastErrorString();
            return false;

        case HTTP_UPDATE_NO_UPDATES:
            _lastError = F("HTTP_UPDATE_NO_UPDATES");
            return false;

        case HTTP_UPDATE_OK:
            _lastError = F("HTTP_UPDATE_OK");
            return true;
    }
    return false;
}

bool ESPGithubUpdater::githubAPICall(String &path, GithubResponseHandler handler) {
    if(!_client) {
        _client = new  BearSSL::WiFiClientSecure;
        bool mfln = _client->probeMaxFragmentLength(GithubAPI, 443, 1024);
        if (mfln) {
            _client->setBufferSizes(1024, 1024);
            Serial.println(F("MFLN ok"));
        }
        _client->setInsecure();
    }
    HTTPClient httpClient;
    _lastError = "";
    String url = F("https://");
    url += GithubAPI;
    url += path;
    Serial.printf_P(PSTR("githubAPICall %s\n"), url.c_str());
    if(!httpClient.begin(*_client, url)) {
        _lastError = F("Begin failed");
        Serial.println(F("begin failed"));
        return false;
    }
    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpClient.setUserAgent(F("ESP Github Updater 0.1"));
    httpClient.addHeader(F("Accept"),F("application/vnd.github.v3+json"));
    if(_user.length() && _token.length()) {
        httpClient.setAuthorization(_user.c_str(),_token.c_str());
    }
    
    int code = httpClient.GET();
    Serial.printf_P(PSTR("githubAPICall: code %d\n"), code);
    bool res = code == 200;
    if(res) {
        if(handler) {
            handler(httpClient);
        }
    } else {
        if (code < 0) {
            _lastError = HTTPClient::errorToString(code); 
        } else {
            _lastError = httpClient.getString();
        }
        Serial.printf_P(PSTR("githubAPICall error %s\n"), _lastError.c_str());
    } 
    httpClient.end();
    return res;
}