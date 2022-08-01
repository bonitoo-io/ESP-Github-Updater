#include "ESPGithubUpdater.h"
#include "HTTPJsonParser.h"
#include <JsonStreamingParser.h>

#ifdef ESP8266
#include "ESP8266httpUpdate.h"
#elif defined(ESP32)
#include "HTTPUpdate.h"
#define ESPhttpUpdate httpUpdate
#endif


const char *DigiCertHighAssuranceEVRootCA PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

static const char *GithubAPI PROGMEM = "api.github.com"; 
static const char *GithubRepoPath PROGMEM = "/repos/%s/%s/releases"; 
static const char *GithubTag PROGMEM = "/tags/%s";
static const char *GithubLastRelease PROGMEM = "?per_page=1&page=1";
static const char *GithubLatestRelease PROGMEM = "/latest";



class ReleaseParser: public JsonListener {
private:
    enum parserState {
        release = 0,
        assets,
        assetBinFile,
        assetMD5File
    };
    String _key;
    parserState _state;
    bool _skipAsset = false;
    releaseInfo *_release = nullptr;
    const char *_binFileName;
    const char *_md5FileName;
public:
    ReleaseParser(const char *binFileName, const char *md5FileName):_state(parserState::release),_binFileName(binFileName),_md5FileName(md5FileName) {}
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
    if(_state == parserState::release) {
        if(!_release) {
            _release = new releaseInfo; 
        }
    }
}

void ReleaseParser::key(String key) {
    if(_state == parserState::release && key == F("assets")) {
        _state = parserState::assets;
    }
    _key = key;
};

void ReleaseParser:: endArray() {
    if(_state != parserState::release) {
        _state = parserState::release;
    }
}

bool fileNameMatches(const char *tmplt, const char *file) {
    //Serial.printf("match: %s vs %s\n", tmplt, file);
    char *p = strstr_P(tmplt, PSTR("%version%"));
    bool match = false;
    if(p) {
        int pos = p-tmplt;
        int len = pos+1;
        char *preffix = new char[len];
        strncpy(preffix, tmplt,  len-1);
        preffix[len-1] = 0;
        //Serial.printf("   preffix: %s,%d\n", preffix, len);
        if(strstr(file, preffix)) {
            len = strlen(tmplt)-pos-9+1;//9-strlen("%version%")
            if(len>0) {
                char *suffix = new char[len];
                strncpy(suffix, tmplt+pos+9,  len-1);
                suffix[len-1] = 0;
                //Serial.printf("   suffix: %s,%d\n", suffix, len);
                if(strstr(file, suffix)) {
                    match = true;
                }
                delete [] suffix;
            } else {
                match = true;
            }
        }
        delete [] preffix;
    } else {
        match = strcmp(tmplt, file) == 0;
    }
    //Serial.printf("  result: %s\n", match?"true":"false");
    return match;
}

void ReleaseParser::value(String value) {
    //Serial.println("key: " + _key + " value: " + value);
    if(_state == parserState::release) {
        if ( _key == F("name")) {
            _release->version = value;
        } else if ( _key == F("tag_name")) {
            _release->tag = value;
        }
    } else {
        if ( _key == F("name")) {
            if(fileNameMatches(_binFileName, value.c_str())) {
                _state = parserState::assetBinFile;
            } else if(_md5FileName && fileNameMatches(_md5FileName, value.c_str())) {
                _state = parserState::assetMD5File;
            } else {
                _state = parserState::assets;
            }
        }
        if ( _key == F("browser_download_url")) {
            if(_state == parserState::assetBinFile) {
                _release->binFileURL = value;
            } else if(_state == parserState::assetMD5File) {
                _release->md5FileURL = value;
            }
        } 
    }
}

ESPGithubUpdater::ESPGithubUpdater(String owner, String repoName, String fileName):
    _owner(owner), _repoName(repoName), _fileName(fileName) {
     
}


 ESPGithubUpdater::~ESPGithubUpdater() { 
#ifdef ESP8266     
     delete _cert;
#endif     
     delete _client; 
}

void ESPGithubUpdater::setAuthorization(String user, String token) {
    _user = user;
    _token = token;
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
        ReleaseParser rel(_fileName.c_str(), _md5File.length()?_md5File.c_str():nullptr);
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
    int length = strlen_P(GithubRepoPath) + _owner.length()+_repoName.length() + version.length() + strlen_P(GithubLastRelease) +1;
    char *buff = new char[length];
    sprintf_P(buff, GithubRepoPath, _owner.c_str(), _repoName.c_str());
    if(version.length() > 0) {
         sprintf_P(buff+strlen(buff), GithubTag, version.c_str());
    } else {
        if(includePrelease) {
            sprintf_P(buff+strlen(buff),GithubLastRelease);
        } else {
            sprintf_P(buff+strlen(buff),GithubLatestRelease);
        }
    }
    path = buff;
    delete [] buff;
    return path;
}

bool ESPGithubUpdater::runUpdate(String version, UpdateProgressHandler handler) {
    if(!fetchVersion(version)) {
        return false;
    }
    //Serial.printf_P(PSTR("Update %s, MD5file: %s\n"), _cache.binFileURL.c_str(),_cache.md5FileURL.c_str());
    if(!_cache.binFileURL.length()) {
        //Serial.println(F(" Bin file not found"));
        _lastError = String(F("Bin file not found: ")) + _fileName;
        return false;
    }
    if(_md5File.length()) {
        if(_cache.md5FileURL.length()) {
            String md5;
            int code = getMD5Sum( _cache.md5FileURL, md5);
            if(!code) {
                //Serial.printf_P(PSTR("  MD5: %s\n"), md5.c_str());
                ESPhttpUpdate.setMD5sum(md5);
            } else {
                return false;
            }
        } else {
            //Serial.println(F(" MD5 file not found"));
            _lastError = String(F("MD5 file not found: ")) + _md5File;
            return false;
        }
    }
#ifdef ESP8266      
    if(_user.length() && _token.length()) {
        ESPhttpUpdate.setAuthorization(_user.c_str(),_token.c_str());
    }
    ESPhttpUpdate.closeConnectionsOnUpdate(false);
#endif    
    if(handler) {
        ESPhttpUpdate.onProgress([handler](size_t act, size_t total) {
            float prog = act;
            prog = (prog/total)*100;
            handler((int)prog);
        });
    }
    
    ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    ESPhttpUpdate.rebootOnUpdate(_restartOnUpdate);
    t_httpUpdate_return ret = ESPhttpUpdate.update(*_client, _cache.binFileURL);
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
#ifdef ESP8266        
        _client = new  BearSSL::WiFiClientSecure;
        bool mfln = _client->probeMaxFragmentLength(GithubAPI, 443, 1024);
        if (mfln) {
            _client->setBufferSizes(1024, 1024);
            //Serial.println(F("MFLN ok"));
        }
#elif defined(ESP32)
        _client =  new WiFiClientSecure;  
#endif        
        if(_insecure) {
            _client->setInsecure();
        } else {
#ifdef ESP8266                    
            if(!_cert) {
                _cert = new BearSSL::X509List(DigiCertHighAssuranceEVRootCA); 
            }
            _client->setTrustAnchors(_cert);
#elif defined(ESP32)
            _client->setCACert(DigiCertHighAssuranceEVRootCA);
#endif            
        }
    }
    HTTPClient httpClient;
    _lastError = "";
    String url = F("https://");
    url += FPSTR(GithubAPI);
    url += path;
    //Serial.printf_P(PSTR("githubAPICall %s\n"), url.c_str());
    if(!httpClient.begin(*_client, url)) {
        _lastError = F("Begin failed");
        //Serial.println(F("begin failed"));
        return false;
    }
    httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpClient.setUserAgent(F("ESP Github Updater 1.0.0"));
    httpClient.addHeader(F("Accept"),F("application/vnd.github.v3+json"));
    if(_user.length() && _token.length()) {
        httpClient.setAuthorization(_user.c_str(),_token.c_str());
    }
    
    int code = httpClient.GET();
    //Serial.printf_P(PSTR("githubAPICall: code %d\n"), code);
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
        //Serial.printf_P(PSTR("githubAPICall error %s\n"), _lastError.c_str());
    } 
    httpClient.end();
    return res;
}

int ESPGithubUpdater::getMD5Sum(const String &url, String &md5) {
  HTTPClient httpClient;
  //Serial.printf_P(PSTR("Download: %s\n"), url.c_str());
  httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpClient.begin(*_client, url);
  int code = httpClient.GET();
  int errorCode = 0;
  String ret;
  if(code == 200) {
    uint32_t size = httpClient.getSize();
    //Serial.printf("   Size: %d\n", size);
    if(size >32 && size<200) {
        errorCode = 0;
        md5 = httpClient.getString().substring(0,32);
    } else {
        _lastError = String(F("Ivalid MD5 file length: ")) +String(size);
        errorCode = -1;
    }
  } else {
    errorCode = code;
    if (code < 0) {
        _lastError = HTTPClient::errorToString(code); 
    } else {
        _lastError = httpClient.getString();
    }
  }
  httpClient.end();
  return errorCode;
}

