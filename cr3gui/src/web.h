#ifndef __CURLIT_HEADER_FILE__
#define __CURLIT_HEADER_FILE__
 
#include <map>
#include <string>
#include <vector>
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
    const char * method;    // "PUT", "POST"...; NULL: GET, or POST when there is a body
    string body;            // sent with the request (JSON of the position sync); empty: none
    vector<string> headers; // more lines of the header, "Name: value"
    // false: a redirect is not followed (libcurl sends the headers above to the new address,
    // another host too: the key of the position sync goes nowhere else); its address is given
    // in redirectUrl, and the caller decides
    bool followRedirects;
    // called during the transfer with the bytes got and the bytes expected (0: unknown);
    // false stops the transfer
    bool (*progress)(double now, double total, void * data);
    void * progressData;
    // results
    long httpCode;          // 0 when the address is not HTTP
    int curlCode;           // CURLE_OK (0) on success
    string error;           // text of the libcurl error
    string redirectUrl;     // followRedirects false: the address a 3xx answer names; empty: none

    WebRequest()
        : connectTimeout(20), timeout(60), verifyPeer(false), caInfo(NULL), userAgent(NULL), accept(NULL),
          method(NULL), followRedirects(true), progress(NULL), progressData(NULL), httpCode(0), curlCode(0)
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
    // the file of the root certificates of the firmware; NULL when it has none of the known ones
    static const char * caFile();
    // The host of an address, in lower case, without the scheme, a login written in it, the port and
    // the path: "https://u:p@Host:8080/a" -> "host", "[::1]:80" -> "[::1]". The login of a catalog
    // and the key of the position sync go to the host they were set for only.
    static string hostOf(const string &url);
    // closes the connections kept between the requests; called when the program ends
    static void cleanup();
    // The login and the password of a catalog (the OPDS browser sets them when it opens one): get()
    // and getBinary() send them, by HTTP Basic or Digest as the server asks, only to an address on
    // the host of the address given here; empty user: none.
    static void setCredentials(const string &url, const string &user, const string &password);
    // the HTTP answer of the last get() or getBinary(): 401 or 403 when the server wants a login
    static long lastHttpCode;
private:
    static string authHost, authUser, authPassword;
    // sets the login on the handle when the address is on the host of the login
    static void applyCredentials(CURL *curl, const string &url);

};
#endif
