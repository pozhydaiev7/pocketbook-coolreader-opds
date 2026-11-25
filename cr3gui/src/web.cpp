#include <crengine.h>
#include <pthread.h>
#include <unistd.h>
#include "web.h"

// One request at a time, whichever thread makes it. The automatic position sync makes its
// requests in a thread of its own (pb_kosync.cpp, AutoJob), and the handle that is kept, the
// login of a catalog and the TLS library of the firmware are shared by all the requests. A
// request made inside another one by the same thread (from the progress callback of a download)
// goes on as before: the lock is taken again by the thread that holds it.
static pthread_mutex_t webMutex;
static pthread_once_t webMutexOnce = PTHREAD_ONCE_INIT;

static void webMutexInit()
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&webMutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

class WebLock
{
    WebLock(const WebLock &);
    WebLock & operator = (const WebLock &);
public:
    WebLock()
    {
        pthread_once(&webMutexOnce, webMutexInit);
        pthread_mutex_lock(&webMutex);
    }
    ~WebLock() { pthread_mutex_unlock(&webMutex); }
};
 
//have to re-declare static members at file level
char web::errorBuffer[CURL_ERROR_SIZE];
string web::buffer;
long web::lastHttpCode = 0;
string web::authHost;
string web::authUser;
string web::authPassword;

string web::hostOf(const string &url)
{
    size_t start = url.find("://");
    start = start == string::npos ? 0 : start + 3;
    // a login written in the address ("user:password@host") is no part of the host
    const size_t authority = url.find_first_of("/?#", start);
    const size_t at = url.rfind('@', authority == string::npos ? string::npos : authority);
    if (at != string::npos && at >= start && (authority == string::npos || at < authority))
        start = at + 1;
    size_t end;
    if (start < url.size() && url[start] == '[')
        end = url.find(']', start) == string::npos ? string::npos : url.find(']', start) + 1;
    else
        end = url.find_first_of(":/?#", start);
    string host = url.substr(start, end == string::npos ? string::npos : end - start);
    for (size_t i = 0; i < host.size(); i++)
        if (host[i] >= 'A' && host[i] <= 'Z')
            host[i] = (char)(host[i] - 'A' + 'a');
    return host;
}

void web::setCredentials(const string &url, const string &user, const string &password)
{
    WebLock lock;
    authHost = user.empty() ? string() : hostOf(url);
    authUser = user;
    authPassword = password;
}

// The login goes to the host of the catalog only: a book or a cover kept on another server (a
// CDN) does not get it. libcurl does not send it on a redirect to another host either
// (CURLOPT_UNRESTRICTED_AUTH stays off). A catalog of Calibre asks for Digest, most others
// for Basic: libcurl answers what the server asks for.
void web::applyCredentials(CURL *curl, const string &url)
{
    if (authUser.empty() || authHost.empty() || hostOf(url) != authHost)
        return;
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, (long)(CURLAUTH_BASIC | CURLAUTH_DIGEST));
    curl_easy_setopt(curl, CURLOPT_USERNAME, authUser.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, authPassword.c_str());
}

// One libcurl handle serves every request of the program. A handle made and dropped for each
// request opened a new connection every time: name lookup, TCP and the TLS handshake again, 0.3
// to 0.65 s for every page of a catalog on an InkPad 4. A handle that stays keeps the connection
// to the server, the names it has looked up and the TLS session; curl_easy_reset() clears the
// options of the request before and nothing of that. libcurl opens a new connection by itself
// when the kept one has been closed (the server, Wi-Fi turned off).
static CURL *sharedHandle = NULL;
static bool sharedHandleBusy = false;

// The handle of one request. A request made while another one is on its way (from the progress
// callback of a download) cannot share its handle and gets one of its own, dropped at the end.
class WebHandle
{
    CURL *_curl;
    bool _own;
public:
    WebHandle() : _curl(NULL), _own(sharedHandleBusy)
    {
        if (_own) {
            _curl = curl_easy_init();
            return;
        }
        if (sharedHandle == NULL)
            sharedHandle = curl_easy_init();
        else
            curl_easy_reset(sharedHandle);
        _curl = sharedHandle;
        sharedHandleBusy = _curl != NULL;
    }
    ~WebHandle()
    {
        if (_own) {
            if (_curl != NULL)
                curl_easy_cleanup(_curl);
        } else {
            sharedHandleBusy = false;
        }
    }
    CURL *get() { return _curl; }
};

void web::cleanup()
{
    WebLock lock;
    if (sharedHandle != NULL && !sharedHandleBusy) {
        // the cookies of the handle are written to their file here
        curl_easy_cleanup(sharedHandle);
        sharedHandle = NULL;
    }
}

// Where the time of a request went, for the log: the moments from its start, in ms
static void webLogTimes(CURL *curl, const char *what, const string &url)
{
    if (!CRLog::isInfoEnabled())
        return;
    double lookup = 0, connected = 0, secured = 0, firstByte = 0, total = 0, bytes = 0;
    long connections = 0;
    curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &lookup);
    curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connected);
    curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &secured);
    curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &firstByte);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &bytes);
    curl_easy_getinfo(curl, CURLINFO_NUM_CONNECTS, &connections);
    CRLog::info("perf: network %s: total %.0f ms; name found at %.0f, connected at %.0f, TLS ready at %.0f, "
                "first byte at %.0f; %.0f bytes over the network, %s; %s", what, total * 1000, lookup * 1000,
                connected * 1000, secured * 1000, firstByte * 1000, bytes,
                connections == 0 ? "the kept connection" : "a new connection", url.c_str());
}

// Asks for a compressed answer: a feed of a catalog is XML and comes 5 to 10 times smaller.
// libcurl unpacks it. Not for files: their size on the way is what the progress is counted by.
static void webAcceptCompressed(CURL *curl)
{
#if LIBCURL_VERSION_NUM >= 0x071506
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
#else
    curl_easy_setopt(curl, CURLOPT_ENCODING, "");
#endif
}

//---------------------------------------------------------------------------
string web::get(const string &url)
{
    bool   usepost=false;
    string params ="";
    return easycurl(url, usepost, params);
}
//---------------------------------------------------------------------------
bool web::getBinary(const string &url, const string &outfilename)
{
    CRLog::trace("web::getBinary(%s, %s)", url.c_str(), outfilename.c_str());

    bool   usepost=false;
    string params ="";
    return easycurlBinary(url, usepost, params, outfilename);
}
//---------------------------------------------------------------------------
string web::get(const string &url, map<string, string> &m)
{
    bool usepost=false;
    string poststring="";
 
    map<string, string>::iterator curr,end;
    for(curr = m.begin(), end = m.end(); curr != end; curr++)
    {
        poststring+= curr->first + "=" + web::urlencode(curr->second)+ "&";
    }
 
    return easycurl(url, usepost, poststring);
}
//---------------------------------------------------------------------------
string web::post(const string &url, map<string, string> &m)
{
    bool usepost=true;
    string poststring="";
 
    map<string, string>::iterator curr,end;
    for(curr = m.begin(), end = m.end(); curr != end; curr++)
    {
        poststring+= curr->first + "=" + web::urlencode(curr->second)+ "&";
    }
 
    return easycurl(url, usepost, poststring);
}
//---------------------------------------------------------------------------
string web::easycurl(const string &url, bool post, const string &postparamstring)
{
    WebLock lock;
    CRLog::trace("web::easycurl(%s)", url.c_str());
 
    // Our curl objects
    buffer="";
    errorBuffer[0]=0;
 
    CURL *curl;
    CURLcode result;
 
    // Create curl handle
    CRLog::trace("web::easycurl(): Create curl handle");
    WebHandle handle;
    curl = handle.get();
 
    if (curl)
    {
      // Now set up all of the curl options
      curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str() );
      curl_easy_setopt(curl, CURLOPT_HEADER, 0);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
      if (post)
      {
          curl_easy_setopt(curl, CURLOPT_POST,1);
          curl_easy_setopt(curl, CURLOPT_POSTFIELDS,postparamstring.c_str());
      }
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)");
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);  // this line makes it work under https
      curl_easy_setopt(curl, CURLOPT_COOKIEFILE, LIB_CURL_COOKIES_FILE);//read from
      curl_easy_setopt(curl, CURLOPT_COOKIEJAR, LIB_CURL_COOKIES_FILE); //write to
      // Never block the UI for minutes: fail fast when the server does not answer.
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
      webAcceptCompressed(curl);
      applyCredentials(curl, url);

      // Attempt to retrieve the remote page
      CRLog::trace("web::easycurl(): Attempt to retrieve the remote page");
      result = curl_easy_perform(curl);
      long httpCode = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      lastHttpCode = httpCode;
      webLogTimes(curl, "page", url);
      // the handle stays (see WebHandle): the cookies it got go to their file now
      curl_easy_setopt(curl, CURLOPT_COOKIELIST, "FLUSH");

      // Did we succeed?
      if (result == CURLE_OK)
      {
          CRLog::info("web::easycurl(%s): HTTP %ld, %d bytes", url.c_str(), httpCode, (int)buffer.length());
          return buffer;
      }
      else
      {
          CRLog::error("web::easycurl(%s): curl error %d: %s", url.c_str(), (int)result, errorBuffer);
          return "";
      }
    }
 
    return "";
}
//---------------------------------------------------------------------------
bool web::easycurlBinary(const string &url, bool post, const string &postparamstring, const string &outfilename)
{
    WebLock lock;
    CRLog::trace("web::easycurlBinary(%s, bool, %s, %s)", url.c_str(), postparamstring.c_str(), outfilename.c_str());
 
    // Our curl objects
    errorBuffer[0]=0;
 
    FILE *fp;
    CURL *curl;
    CURLcode result;
 
    // Create curl handle
    CRLog::trace("web::easycurlBinary(): Create curl handle");
    WebHandle handle;
    curl = handle.get();
 
    if (curl)
    {
      // Open file for writing
      CRLog::trace("web::easycurlBinary(): Open file for writing");
      fp = fopen(outfilename.c_str(),"wb");
      if (!fp) {
          CRLog::error("web::easycurlBinary(): cannot open %s for writing", outfilename.c_str());
          return false;
      }

      // Now set up all of the curl options
      CRLog::trace("web::easycurlBinary(): Now set up all of the curl options");
      curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str() );
      curl_easy_setopt(curl, CURLOPT_HEADER, 0);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writerBinary);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
      if (post)
      {
          curl_easy_setopt(curl, CURLOPT_POST,1);
          curl_easy_setopt(curl, CURLOPT_POSTFIELDS,postparamstring.c_str());
      }
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2);
      curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)");
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);  // this line makes it work under https
      curl_easy_setopt(curl, CURLOPT_COOKIEFILE, LIB_CURL_COOKIES_FILE);//read from
      curl_easy_setopt(curl, CURLOPT_COOKIEJAR, LIB_CURL_COOKIES_FILE); //write to
      // Fail when the server does not answer or the transfer stalls (no total limit for big books).
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
      curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
      applyCredentials(curl, url);

      // Attempt to retrieve the remote page
      CRLog::trace("web::easycurlBinary(): Attempt to retrieve the remote binary");
      result = curl_easy_perform(curl);
      long httpCode = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      lastHttpCode = httpCode;

      webLogTimes(curl, "file", url);
      // the handle stays (see WebHandle): the cookies it got go to their file now
      curl_easy_setopt(curl, CURLOPT_COOKIELIST, "FLUSH");

      // Close file handle
      CRLog::trace("web::easycurlBinary(): Close file handle");
      fclose(fp);

      // Did we succeed?
      if (result == CURLE_OK)
      {
          CRLog::info("web::easycurlBinary(%s): HTTP %ld", url.c_str(), httpCode);
          return true;
      }
      else
      {
          CRLog::error("web::easycurlBinary(%s): curl error %d: %s", url.c_str(), (int)result, errorBuffer);
          return false;
      }
    }
 
    CRLog::trace("web::easycurlBinary(): Couldn't initialize curl");
    return false;
}
//---------------------------------------------------------------------------
static size_t requestToString(char *data, size_t size, size_t nmemb, void *target)
{
    ((string *)target)->append(data, size * nmemb);
    return size * nmemb;
}

static size_t requestToFile(char *data, size_t size, size_t nmemb, void *target)
{
    return fwrite(data, size, nmemb, (FILE *)target);
}

// libcurl stops the transfer when the callback returns non-zero
static int requestProgress(void *data, double dltotal, double dlnow, double ultotal, double ulnow)
{
    (void)ultotal;
    (void)ulnow;
    WebRequest *rq = (WebRequest *)data;
    return rq->progress(dlnow, dltotal, rq->progressData) ? 0 : 1;
}

bool web::request(const string &url, WebRequest &rq, string *body, const char *outFile)
{
    WebLock lock;
    rq.httpCode = 0;
    rq.curlCode = CURLE_FAILED_INIT;
    rq.error.clear();
    rq.redirectUrl.clear();
    if (body)
        body->clear();

    WebHandle handle;
    CURL *curl = handle.get();
    if (!curl) {
        rq.error = "curl_easy_init failed";
        return false;
    }
    FILE *fp = NULL;
    if (outFile) {
        fp = fopen(outFile, "wb");
        if (!fp) {
            rq.curlCode = CURLE_WRITE_ERROR;
            rq.error = string("cannot write ") + outFile;
            return false;
        }
    }
    char errorText[CURL_ERROR_SIZE];
    errorText[0] = 0;
    struct curl_slist *headers = NULL;
    if (rq.accept)
        headers = curl_slist_append(headers, (string("Accept: ") + rq.accept).c_str());
    for (size_t i = 0; i < rq.headers.size(); i++)
        headers = curl_slist_append(headers, rq.headers[i].c_str());
    if (!rq.body.empty())
        headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorText);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, rq.followRedirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    if (fp) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, requestToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)fp);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, requestToString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)body);
    }
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    // a body makes a POST; another method is put over it (PUT of the position sync)
    if (!rq.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, rq.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)rq.body.size());
    }
    if (rq.method)
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, rq.method);
    if (rq.userAgent)
        curl_easy_setopt(curl, CURLOPT_USERAGENT, rq.userAgent);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, rq.verifyPeer ? 2L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, rq.verifyPeer ? 1L : 0L);
    if (rq.verifyPeer && rq.caInfo)
        curl_easy_setopt(curl, CURLOPT_CAINFO, rq.caInfo);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, rq.connectTimeout);
    if (rq.timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, rq.timeout);
    } else {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    }
    if (rq.progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, requestProgress);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&rq);
    }

    if (!fp)
        webAcceptCompressed(curl);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rq.httpCode);
    if (!rq.followRedirects) {
        char * location = NULL;
        if (curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &location) == CURLE_OK && location)
            rq.redirectUrl = location;
    }
    webLogTimes(curl, fp ? "file" : "page", url);
    // The handle stays (see WebHandle). The list of headers and the text of the error belong
    // to this call: the handle must not keep their addresses.
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, (struct curl_slist *)NULL);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, (char *)NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (char *)NULL);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, (char *)NULL);
    if (headers)
        curl_slist_free_all(headers);
    if (fp && fclose(fp) != 0 && result == CURLE_OK)
        result = CURLE_WRITE_ERROR;

    rq.curlCode = (int)result;
    if (result != CURLE_OK)
        rq.error = errorText[0] ? errorText : curl_easy_strerror(result);
    CRLog::info("web::request(%s): curl %d, HTTP %ld, certificate check %s%s%s", url.c_str(), (int)result,
                rq.httpCode, rq.verifyPeer ? "on" : "off", rq.error.empty() ? "" : ": ", rq.error.c_str());
    return result == CURLE_OK;
}

bool web::isCertificateError(int curlCode)
{
    return curlCode == CURLE_SSL_CACERT || curlCode == CURLE_PEER_FAILED_VERIFICATION
        || curlCode == CURLE_SSL_CACERT_BADFILE;
}

const char * web::caFile()
{
    // the root certificates of the firmware, in the places firmwares keep them
    static const char * const files[] = {
        "/ebrmain/share/ssl/certs/ca-certificates.crt",
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/ssl/cert.pem",
        NULL
    };
    for (int i = 0; files[i]; i++)
        if (access(files[i], R_OK) == 0)
            return files[i];
    return NULL;
}
//---------------------------------------------------------------------------
int web::writer(char *data, size_t size, size_t nmemb, string *buffer)
{
  int result = 0;
  if (buffer != NULL)
  {
    buffer->append(data, size * nmemb);
    result = size * nmemb;
  }
  return result;
}
//---------------------------------------------------------------------------
size_t web::writerBinary(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}
//---------------------------------------------------------------------------
string web::urlencode(const string &c)
{
 
    string escaped="";
    int max = c.length();
    for(int i=0; i<max; i++)
    {
        if ( (48 <= c[i] && c[i] <= 57) ||//0-9
             (65 <= c[i] && c[i] <= 90) ||//abc...xyz
             (97 <= c[i] && c[i] <= 122) || //ABC...XYZ
             (c[i]=='~' || c[i]=='!' || c[i]=='*' || c[i]=='(' || c[i]==')' || c[i]=='\'') //~!*()'
        )
        {
            escaped.append( &c[i], 1);
        }
        else
        {
            escaped.append("%");
            escaped.append( char2hex(c[i]) );//converts char 255 to string "ff"
        }
    }
    return escaped;
}
//-----------------------------------------------------------------------------
string web::char2hex( char dec )
{
    char dig1 = (dec&0xF0)>>4;
    char dig2 = (dec&0x0F);
    if ( 0<= dig1 && dig1<= 9) dig1+=48;    //0,48inascii
    if (10<= dig1 && dig1<=15) dig1+=97-10; //a,97inascii
    if ( 0<= dig2 && dig2<= 9) dig2+=48;
    if (10<= dig2 && dig2<=15) dig2+=97-10;
 
    string r;
    r.append( &dig1, 1);
    r.append( &dig2, 1);
    return r;
}
//-----------------------------------------------------------------------------
