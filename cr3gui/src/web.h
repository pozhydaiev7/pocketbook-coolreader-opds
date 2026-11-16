#ifndef __CURLIT_HEADER_FILE__
#define __CURLIT_HEADER_FILE__
 
#include <map>
#include <string>
#include <iostream>
 
#include <curl/curl.h>

#define LIB_CURL_COOKIES_FILE "/mnt/ext1/system/config/cr3/cr3_cookies.txt"

using namespace std;

// A request with its own limits and a progress callback (the program update).
struct WebRequest
{
    long connectTimeout;    // seconds
    long timeout;           // seconds for the whole transfer; 0: no limit, a stalled transfer fails
    bool verifyPeer;        // check the certificate of the server
    const char * caInfo;    // file of root certificates; NULL: the default of libcurl
    const char * userAgent; // NULL: none
    const char * accept;    // value of the Accept header; NULL: none
    // called during the transfer with the bytes got and the bytes expected (0: unknown);
    // false stops the transfer
    bool (*progress)(double now, double total, void * data);
    void * progressData;
    // results
    long httpCode;          // 0 when the address is not HTTP
    int curlCode;           // CURLE_OK (0) on success
    string error;           // text of the libcurl error

    WebRequest()
        : connectTimeout(20), timeout(60), verifyPeer(false), caInfo(NULL), userAgent(NULL), accept(NULL),
          progress(NULL), progressData(NULL), httpCode(0), curlCode(0)
    {
    }
};

class web
{
protected:    // User declarations
    static char errorBuffer[CURL_ERROR_SIZE];
    static string buffer;
    static int writer(char *data, size_t size, size_t nmemb, string *buffer);
    static size_t writerBinary(void *ptr, size_t size, size_t nmemb, FILE *stream);
    static string easycurl(const string &url, bool post, const string &postparamstring);
    static bool easycurlBinary(const string &url, bool post, const string &postparamstring, const string &outfilename);
    static string urlencode(const string &c);
    static string char2hex(char dec);    
 
public:        // User declarations
    web(){};
    ~web(){};
    static string post(const string &url, map<string, string> &abbrevs);
    static string get(const string &url, map<string, string> &abbrevs);
    static string get(const string &url);
    static bool getBinary(const string &url, const string &outfilename);
    // Gets the address into body or, when outFile is given, into that file. Redirects are followed.
    // True when the transfer ended without a libcurl error: check rq.httpCode for the answer.
    static bool request(const string &url, WebRequest &rq, string *body, const char *outFile);
    // the libcurl error means the certificate of the server was not accepted
    static bool isCertificateError(int curlCode);

};
#endif
