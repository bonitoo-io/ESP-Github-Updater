#ifndef HTTP_JSON_PARSER_H
#define HTTP_JSON_PARSER_H

#include <Stream.h>
#include <JsonStreamingParser.h>

class HTTPJsonParser : public Stream {
private:
    JsonStreamingParser parser;
public:
    HTTPJsonParser(JsonListener* listener) {
        parser.setListener(listener);
    }

    // Stream overrides
    virtual int available() override
    {
        return 0;
    }

    virtual int availableForWrite() override
    {
        return 256;
    }

    virtual int read() override {
        return 0;
    }
    virtual int peek()  override {
        return 0;
    }
    virtual size_t write(uint8_t data) override  {
        //Serial.printf(" write %d\n", data);
        parser.parse(data);
        return 1;
    }

    virtual size_t write(const uint8_t* buffer, size_t len) override
    {
        //Serial.printf(" write buff len %d\n", len);
        for(size_t i = 0; i<len;i++) {
            parser.parse(buffer[i]);
        }
        return len;
    } 
#ifdef ESP8266
    virtual bool outputCanTimeout() override
    {
        return false;
    } 

    virtual bool hasPeekBufferAPI() const override
    {
        return false;
    } 
#endif    
};

#endif //HTTP_JSON_PARSER_H