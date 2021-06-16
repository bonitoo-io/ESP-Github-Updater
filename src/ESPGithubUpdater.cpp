#include "ESPGIthubUpdater.h"
#include "HTTPJsonParser.h"
#include <JsonStreamingParser.h>
#include <ESP8266httpUpdate.h>


const char *DigiCertHighAssuranceEVRootCA PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDxTCCAq2gAwIBAgIQAqxcJmoLQJuPC3nyrkYldzANBgkqhkiG9w0BAQUFADBs
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSswKQYDVQQDEyJEaWdpQ2VydCBIaWdoIEFzc3VyYW5j
ZSBFViBSb290IENBMB4XDTA2MTExMDAwMDAwMFoXDTMxMTExMDAwMDAwMFowbDEL
MAkGA1UEBhMCVVMxFTATBgNVBAoTDERpZ2lDZXJ0IEluYzEZMBcGA1UECxMQd3d3
LmRpZ2ljZXJ0LmNvbTErMCkGA1UEAxMiRGlnaUNlcnQgSGlnaCBBc3N1cmFuY2Ug
RVYgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMbM5XPm
+9S75S0tMqbf5YE/yc0lSbZxKsPVlDRnogocsF9ppkCxxLeyj9CYpKlBWTrT3JTW
PNt0OKRKzE0lgvdKpVMSOO7zSW1xkX5jtqumX8OkhPhPYlG++MXs2ziS4wblCJEM
xChBVfvLWokVfnHoNb9Ncgk9vjo4UFt3MRuNs8ckRZqnrG0AFFoEt7oT61EKmEFB
Ik5lYYeBQVCmeVyJ3hlKV9Uu5l0cUyx+mM0aBhakaHPQNAQTXKFx01p8VdteZOE3
hzBWBOURtCmAEvF5OYiiAhF8J2a3iLd48soKqDirCmTCv2ZdlYTBoSUeh10aUAsg
EsxBu24LUTi4S8sCAwEAAaNjMGEwDgYDVR0PAQH/BAQDAgGGMA8GA1UdEwEB/wQF
MAMBAf8wHQYDVR0OBBYEFLE+w2kD+L9HAdSYJhoIAu9jZCvDMB8GA1UdIwQYMBaA
FLE+w2kD+L9HAdSYJhoIAu9jZCvDMA0GCSqGSIb3DQEBBQUAA4IBAQAcGgaX3Nec
nzyIZgYIVyHbIUf4KmeqvxgydkAQV8GK83rZEWWONfqe/EW1ntlMMUu4kehDLI6z
eM7b41N5cdblIZQB2lWHmiRk9opmzN6cN82oNLFpmyPInngiK3BD41VHMWEZ71jF
hS9OMPagMRYjyOfiZRYzy78aG6A9+MpeizGLYAiJLQwGXFK3xPkKmNEVX58Svnw2
Yzi9RKR/5CYrCsSXaQ3pjOLAEFe4yHYSkVXySGnYvCoCWw9E1CAx2/S6cCZdkGCe
vEsXCS+0yx5DaMkHJ8HSXPfqIbloEpw8nL+e/IBcm2PN7EeqJSdnoDfzAIJ9VNep
+OkuE6N36B9K
-----END CERTIFICATE-----
)EOF";

static const char *GithubAPI PROGMEM = "api.github.com"; 
static const char *GithubRepoPath PROGMEM = "/repos/%s/%s/releases"; 
static const char *GithubTag PROGMEM = "/tags/%s";
static const char *GithubLastRelease PROGMEM = "?per_page=1&page=1";
static const char *GithubLatestRelease PROGMEM = "/latest";



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

ESPGithubUpdater::~ESPGithubUpdater() { 
    delete _cert; 
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
    int length = strlen_P(GithubRepoPath) + _owner.length()+_repoName.length() + version.length() + strlen_P(GithubLastRelease) +1;
    char *buff = new char[length];
    sprintf_P(buff, GithubRepoPath, _owner.c_str(), _repoName.c_str());
    if(version.length() > 0) {
         sprintf_P(buff+strlen(buff), GithubTag, version.c_str());
    } else {
        if(includePrelease) {
            sprintf_P(buff+strlen(buff), GithubLastRelease);
        } else {
            sprintf_P(buff+strlen(buff), GithubLatestRelease);
        }
    }
    path = buff;
    delete [] buff;
    return path;
}

struct Url {
    String host;
    int port;
};

Url parse(const String &curl) {
    Url ret;
    String url = curl;
    String host;
    int port = -1;
    Serial.printf("Url: %s\n", url.c_str());
    int index = url.indexOf(':');
    if(index < 0) {
        return ret;
    }
    String protocol = url.substring(0, index);
    port = -1;

    url.remove(0, (index + 3)); // remove http:// or https://

    if (protocol == "http") {
        // set default port for 'http'
        port = 80;
    } else if (protocol == "https") {
        // set default port for 'https'
        port = 443;
    } else {
        return ret;
    }
    index = url.indexOf('/');
    host = url.substring(0, index);
    url.remove(0, index); // remove host 
    // check Authorization
    index = host.indexOf('@');
    if(index >= 0) {
        host.remove(0, index + 1); // remove auth part including @
    }
    // get port
    index = host.indexOf(':');
    if(index >= 0) {
        String portS = host;
        host = host.substring(0, index); // hostname
        portS.remove(0, (index + 1)); // remove hostname + :
        port = portS.toInt(); // get port
    }
    Serial.printf_P("Host: %s, port: %d\n", host.c_str(), port);
    ret.host = host;
    ret.port = port;
    return ret;
}

bool ESPGithubUpdater::runUpdate(String version, UpdateProgressHandler handler) {
    if(!fetchVersion(version)) {
        return false;
    }
    //Serial.printf_P(PSTR("Update %s, bin: %s\n"), _cache.assetUrl.c_str(),_cache.assetIsBin?F("true"):F("false"));
    if(!_cache.assetIsBin) {
        _lastError = F("Not a binary asset: ");
        _lastError += _cache.assetUrl.c_str();
        return false;
    }
    String binUrl = _cache.assetUrl;
    int code;
    do {
        Url url = parse(binUrl);
       
        BearSSL::WiFiClientSecure client;;
        bool mfln = client.probeMaxFragmentLength(url.host, url.port, 1024);
        if (mfln) {
            client.setBufferSizes(1024, 1024);
            Serial.println(F("MFLN ok"));
        }
        client.setTrustAnchors(_cert);
        //client.setInsecure();
        HTTPClient httpClient;
        const char * headerKeys[] = {"Location", "location"};
        httpClient.collectHeaders(headerKeys, 2);
        // if(_user.length() && _token.length()) {
        //     httpClient.setAuthorization(_user.c_str(),_token.c_str());
        // }
        Serial.printf("Begin: %s\n", binUrl.c_str());
        httpClient.begin(client, binUrl);
        code = httpClient.sendRequest("HEAD");
        Serial.printf("Code: %d\n", code);
        if(code == 302) { 
            if(httpClient.hasHeader("Location")) { 
                binUrl = httpClient.header("Location");
            } else if(httpClient.hasHeader("location")) { 
                binUrl = httpClient.header("location");
            }
        } else if(code != 200) {
            if (code < 0) {
                _lastError = HTTPClient::errorToString(code); 
            } else {
                _lastError = httpClient.getString();
            } 
        }
        httpClient.end();
    } while(code == 302);
    if(code != 200) {
        return false;
    }    
    Url url = parse(binUrl);
       
    BearSSL::WiFiClientSecure client;;
    bool mfln = client.probeMaxFragmentLength(url.host, url.port, 1024);
    if (mfln) {
        client.setBufferSizes(1024, 1024);
        Serial.println(F("MFLN ok"));
    }
    client.setTrustAnchors(_cert);
    // if(_user.length() && _token.length()) {
    //     ESPhttpUpdate.setAuthorization(_user.c_str(),_token.c_str());
    // }
    HTTPClient httpClient;
    Serial.printf("Begin: %s\n", binUrl.c_str());
    httpClient.begin(client, binUrl);
    code = httpClient.GET();
    if(code  == 200) {
        uint32_t size = httpClient.getSize();
        Serial.printf("To read: %d\n", size);
        uint32_t r = 0;;
        uint8_t buff[256];
        auto cl = httpClient.getStreamPtr();
        while(cl->connected()) {
            int read = cl->read(buff, 256);
            if(read < 0) {
                break;
            } else if(!read) {
                delay(10);
                continue;
            }
            r+=read;
        }   
        Serial.printf("Read: %d\n", r);
    }
    httpClient.end();
#if 0    
    if(handler) {
        ESPhttpUpdate.onProgress([handler](size_t act, size_t total) {
            float prog = act;
            prog = (prog/total)*100;
            handler((int)prog);
        });
    }
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binUrl);
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
#endif

    return false;
}

bool ESPGithubUpdater::githubAPICall(String &path, GithubResponseHandler handler) {
    
    BearSSL::WiFiClientSecure client;;
    bool mfln = client.probeMaxFragmentLength(FPSTR(GithubAPI), 443, 1024);
    if (mfln) {
        client.setBufferSizes(1024, 1024);
        //Serial.println(F("MFLN ok"));
    }
    if(!_cert) {
        _cert = new BearSSL::X509List(DigiCertHighAssuranceEVRootCA); 
    }
    client.setTrustAnchors(_cert);
    //client.setInsecure();
    HTTPClient httpClient;
    _lastError = "";
    String url = F("https://");
    url += FPSTR(GithubAPI);
    url += path;
    //Serial.printf_P(PSTR("githubAPICall %s\n"), url.c_str());
    if(!httpClient.begin(client, url)) {
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
