#ifndef NOTIFICATIONMGR_H_
#define NOTIFICATIONMGR_H_

#include <boost/date_time/posix_time/posix_time.hpp>


class NotificationMgr
{
    static constexpr int c_suppressSeconds = 60;

    boost::posix_time::ptime m_lastNotif{boost::posix_time::min_date_time};

public:
    NotificationMgr() = default;
    ~NotificationMgr() = default;

    void update();

private:
    void sendEmail() const;
};

#endif /* NOTIFICATIONMGR_H_ */

