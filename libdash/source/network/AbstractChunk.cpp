/*
 * AbstractChunk.cpp
 *****************************************************************************
 * Copyright (C) 2012, bitmovin Softwareentwicklung OG, All Rights Reserved
 *
 * Email: libdash-dev@vicky.bitmovin.net
 *
 * This source code and its use and distribution, is subject to the terms
 * and conditions of the applicable license agreement.
 *****************************************************************************/

#include "AbstractChunk.h"

using namespace dash::network;
using namespace dash::helpers;
using namespace dash::metrics;

uint32_t AbstractChunk::BLOCKSIZE = 32768;

AbstractChunk::AbstractChunk        ()  :
               connection           (NULL),
               dlThread             (NULL),
               bytesDownloaded      (0)
{
}
AbstractChunk::~AbstractChunk       ()
{
    this->AbortDownload();
    this->blockStream.Clear();
    DestroyThreadPortable(this->dlThread);
}

void    AbstractChunk::AbortDownload                ()
{
    this->stateManager.CheckAndSet(IN_PROGRESS, REQUEST_ABORT);
    this->stateManager.CheckAndWait(REQUEST_ABORT, ABORTED);
}
bool    AbstractChunk::StartDownload                ()
{
    if(this->stateManager.State() != NOT_STARTED)
        return false;
    curl_global_init(CURL_GLOBAL_ALL);
    this->curlm = curl_multi_init();

    this->curl = curl_easy_init();
    curl_easy_setopt(this->curl, CURLOPT_URL, this->AbsoluteURI().c_str());
    curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, CurlResponseCallback);
    curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, (void *)this);
    /* Debug Callback */
    curl_easy_setopt(this->curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(this->curl, CURLOPT_DEBUGFUNCTION, CurlDebugCallback);
    curl_easy_setopt(this->curl, CURLOPT_DEBUGDATA, (void *)this);
    curl_easy_setopt(this->curl, CURLOPT_FAILONERROR, true);

    if(this->HasByteRange())
        curl_easy_setopt(this->curl, CURLOPT_RANGE, this->Range().c_str());

    curl_multi_add_handle(this->curlm, this->curl);
    this->dlThread = CreateThreadPortable (DownloadInternalConnection, this);

    if(this->dlThread == NULL)
        return false;

    this->stateManager.State(IN_PROGRESS);

    return true;
}
bool    AbstractChunk::StartDownload                (IConnection *connection)
{
    if(this->stateManager.State() != NOT_STARTED)
        return false;

    this->connection = connection;
    this->dlThread = CreateThreadPortable (DownloadExternalConnection, this);

    if(this->dlThread == NULL)
        return false;

    this->stateManager.State(IN_PROGRESS);


    return true;
}
int     AbstractChunk::Read                         (uint8_t *data, size_t len)
{
    return this->blockStream.GetBytes(data, len);
}
int     AbstractChunk::Peek                         (uint8_t *data, size_t len)
{
    return this->blockStream.PeekBytes(data, len);
}
int     AbstractChunk::Peek                         (uint8_t *data, size_t len, size_t offset)
{
    return this->blockStream.PeekBytes(data, len, offset);
}
void    AbstractChunk::AttachDownloadObserver       (IDownloadObserver *observer)
{
    this->observers.push_back(observer);
    this->stateManager.Attach(observer);
}
void    AbstractChunk::DetachDownloadObserver       (IDownloadObserver *observer)
{
    uint32_t pos = -1;

    for(size_t i = 0; i < this->observers.size(); i++)
        if(this->observers.at(i) == observer)
            pos = i;

    if(pos != -1)
        this->observers.erase(this->observers.begin() + pos);

    this->stateManager.Detach(observer);
}
void*   AbstractChunk::DownloadExternalConnection   (void *abstractchunk)
{
    AbstractChunk   *chunk  = (AbstractChunk *) abstractchunk;
    block_t         *block  = AllocBlock(chunk->BLOCKSIZE);
    int             ret     = 0;

    int count = 0;
    do
    {
        ret = chunk->connection->Read(block->data, block->len, chunk);
        if(ret > 0)
        {
            block_t *streamblock = AllocBlock(ret);
            memcpy(streamblock->data, block->data, ret);
            chunk->blockStream.PushBack(streamblock);
            chunk->bytesDownloaded += ret;

 //           chunk->NotifyDownloadRateChanged();
        }
        if(chunk->stateManager.State() == REQUEST_ABORT)
            ret = 0;
        count += ret;
    }while(ret);

    double speed = chunk->connection->GetAverageDownloadingSpeed();
    double time = chunk->connection->GetDownloadingTime();
    chunk->NotifyDownloadRateChanged(speed);
    chunk->NotifyDownloadTimeChanged(time);
    DeleteBlock(block);

    if(chunk->stateManager.State() == REQUEST_ABORT)
        chunk->stateManager.State(ABORTED);
    else
        chunk->stateManager.State(COMPLETED);

    chunk->blockStream.SetEOS(true);

    return NULL;
}
void*   AbstractChunk::DownloadInternalConnection   (void *abstractchunk)
{
    AbstractChunk *chunk  = (AbstractChunk *) abstractchunk;

    //chunk->response = curl_easy_perform(chunk->curl);
    int u = 1;
    CURLMcode ret_code;
    int repeats = 0;

    do
    {
        int numfds;

        ret_code = curl_multi_wait(chunk->curlm, NULL, 0, 1000, &numfds);

        if (ret_code != CURLM_OK)
        {
            fprintf(stderr, "CURL Multi Wait failed!!!\n");
            break;
        }

        if (numfds == 0)
        {
            repeats++;
            if (repeats > 1)
            {
             	usleep(100000);
            }
        }
        else
        {
            repeats = 0;
        }

        ret_code = curl_multi_perform(chunk->curlm, &u);

    } while(chunk->stateManager.State() != REQUEST_ABORT && u && ret_code == CURLM_OK);

    double speed;
    double size;
    double time;
    curl_easy_getinfo(chunk->curl, CURLINFO_SPEED_DOWNLOAD,&speed);
    curl_easy_getinfo(chunk->curl, CURLINFO_SIZE_DOWNLOAD, &size);
    curl_easy_getinfo(chunk->curl, CURLINFO_TOTAL_TIME, &time);
    
    std::cout << "Download " << chunk->AbsoluteURI() << " duration: " << (time * 1000000) << " [usec] size " << size << 
            " [bytes] speed " << (speed*8)/1000 << " [kbps] " << std::endl;

    //Speed is in Bps ==> *8 for the bps
    speed = 8*speed;
    //size = 8*size; //Uncomment for the size in bits.
    chunk->NotifyDownloadRateChanged(speed);
    chunk->NotifyDownloadTimeChanged(time);
    curl_easy_cleanup(chunk->curl);
    //curl_global_cleanup();

    curl_multi_cleanup(chunk->curlm);
    if(chunk->stateManager.State() == REQUEST_ABORT)
    {
    	chunk->stateManager.State(ABORTED);
    }
    else
    {
    	chunk->stateManager.State(COMPLETED);
    }

    chunk->blockStream.SetEOS(true);

    return NULL;
}
void    AbstractChunk::NotifyDownloadRateChanged    (double bitrate)
{
    for(size_t i = 0; i < this->observers.size(); i++)
        this->observers.at(i)->OnDownloadRateChanged((uint64_t)bitrate);
}
void    AbstractChunk::NotifyDownloadTimeChanged    (double dnltime)
{
    for(size_t i = 0; i < this->observers.size(); i++)
        this->observers.at(i)->OnDownloadTimeChanged(dnltime);
}
size_t  AbstractChunk::CurlResponseCallback         (void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    AbstractChunk *chunk = (AbstractChunk *)userp;

    if(chunk->stateManager.State() == REQUEST_ABORT)
        return 0;

    block_t *block = AllocBlock(realsize);

    memcpy(block->data, contents, realsize);
    chunk->blockStream.PushBack(block);

    chunk->bytesDownloaded += realsize;
//    chunk->NotifyDownloadRateChanged();

    return realsize;
}
size_t  AbstractChunk::CurlDebugCallback            (CURL *url, curl_infotype infoType, char * data, size_t length, void *userdata)
{
    AbstractChunk   *chunk      = (AbstractChunk *)userdata;

    switch (infoType) {
        case CURLINFO_TEXT:
            break;
        case CURLINFO_HEADER_OUT:
            chunk->HandleHeaderOutCallback();
            break;
        case CURLINFO_HEADER_IN:
            chunk->HandleHeaderInCallback(std::string(data));
            break;
        case CURLINFO_DATA_IN:
            break;
        default:
            return 0;
    }
    return 0;
}
void    AbstractChunk::HandleHeaderOutCallback      ()
{
    HTTPTransaction *httpTransaction = new HTTPTransaction();

    httpTransaction->SetOriginalUrl(this->AbsoluteURI());
    httpTransaction->SetRange(this->Range());
    httpTransaction->SetType(this->GetType());
    httpTransaction->SetRequestSentTime(Time::GetCurrentUTCTimeStr());

    this->httpTransactions.push_back(httpTransaction);
}
void    AbstractChunk::HandleHeaderInCallback       (std::string data)
{
    HTTPTransaction *httpTransaction = this->httpTransactions.at(this->httpTransactions.size()-1);

    if (data.substr(0,4) == "HTTP")
    {
        httpTransaction->SetResponseReceivedTime(Time::GetCurrentUTCTimeStr());
        httpTransaction->SetResponseCode(strtoul(data.substr(9,3).c_str(), NULL, 10));
    }

    httpTransaction->AddHTTPHeaderLine(data);
}
const std::vector<ITCPConnection *>&    AbstractChunk::GetTCPConnectionList    () const
{
    return (std::vector<ITCPConnection *> &) this->tcpConnections;
}
const std::vector<IHTTPTransaction *>&  AbstractChunk::GetHTTPTransactionList  () const
{
    return (std::vector<IHTTPTransaction *> &) this->httpTransactions;
}
