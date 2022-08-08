#include "NotificationMgr.h"

#include <curl/curl.h>

#define EMAIL_URL               "smtps://smtp.zoho.com:465"
#define EMAIL_ADDR_FROM         "<rboyer61@zohomail.com>"
#define EMAIL_ADDR_FROM_FULL    "Rob Boyer " EMAIL_ADDR_FROM
#define EMAIL_ADDR_TO           "<rboyer503@comcast.net>"
#define EMAIL_ADDR_TO_FULL      "Rob Boyer " EMAIL_ADDR_TO


static const char* payload_text = "To: " EMAIL_ADDR_TO_FULL "\n"
                                  "From: " EMAIL_ADDR_FROM_FULL "\n"
                                  "Subject: pi-client alert\n\n"
                                  "Motion detected on rpi4-1.\n";

struct upload_status {
    size_t bytes_read;
};


static size_t payload_source(char * ptr, size_t size, size_t nmemb, void * userp)
{
    struct upload_status * upload_ctx = (struct upload_status *)userp;
    const char * data;
    size_t room = size * nmemb;

    if ((size == 0) || (nmemb == 0) || ((size * nmemb) < 1))
    {
        return 0;
    }

    data = &payload_text[upload_ctx->bytes_read];

    if (data)
    {
        size_t len = strlen(data);

        if (room < len)
            len = room;

        memcpy(ptr, data, len);
        upload_ctx->bytes_read += len;

        return len;
    }

    return 0;
}


void NotificationMgr::update()
{
    // Limit notifications to once per c_suppressSeconds.
    const auto currTime = boost::posix_time::microsec_clock::local_time();
    const auto diff = currTime - m_lastNotif;

    if (diff.total_seconds() >= c_suppressSeconds)
    {
        m_lastNotif = currTime;

        std::cout << "Sending notification..." << std::endl;
        sendEmail();
    }
}

void NotificationMgr::sendEmail() const
{
    CURL * curl;
    CURLcode res = CURLE_OK;
    struct curl_slist * recipients = nullptr;
    struct upload_status upload_ctx = {0};

    curl = curl_easy_init();

    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, EMAIL_URL);
        curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_REQUIRED);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, EMAIL_ADDR_FROM);

        recipients = curl_slist_append(recipients, EMAIL_ADDR_TO);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
            std::cout << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }
}
