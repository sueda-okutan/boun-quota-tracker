#include "logic.h"
#include "parser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

QuotaMonitor::QuotaMonitor(QObject* parent)
    : QObject(parent)
    , client_(new QuotaNetworkClient(this))
    , timer_(new QTimer(this))
{
    timer_->setInterval(intervalSeconds_ * 1000);
    connect(timer_, &QTimer::timeout, this, &QuotaMonitor::pollAll);

    connect(client_, &QuotaNetworkClient::quotaHtmlFetched,
            this, &QuotaMonitor::onHtmlFetched);
    connect(client_, &QuotaNetworkClient::quotaFetchFailed,
            this, &QuotaMonitor::onFetchFailed);
}

bool QuotaMonitor::parseCourseLine(
    const QString& line,
    const QString& semester,
    CourseRequest& out,
    QString& error)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        error = "Empty line.";
        return false;
    }

    // Department (letters), 3-4 digit course number, then section separated by
    // a dot and/or whitespace.
    static const QRegularExpression re(
        R"(^\s*([A-Za-z]+)\s*(\d{3,4})[\.\s]+(\d{1,2})\s*$)");

    QRegularExpressionMatch m = re.match(trimmed);
    if (!m.hasMatch()) {
        error = "Could not parse \"" + trimmed +
                "\". Expected e.g. \"CMPE 150.02\".";
        return false;
    }

    out.departmentCode = m.captured(1).toUpper();
    out.courseNumber = m.captured(2);

    // Normalize section to two digits ("2" -> "02").
    QString section = m.captured(3);
    if (section.size() == 1) {
        section.prepend('0');
    }
    out.section = section;

    out.semester = semester;
    return true;
}

QStringList QuotaMonitor::setCoursesFromText(const QString& text, const QString& semester) {
    semester_ = semester;
    courses_.clear();

    // A fresh course set means previous availability state no longer applies.
    previousAvailability_.clear();
    seenCourses_.clear();

    QStringList invalidLines;
    const QStringList lines = text.split('\n');
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        CourseRequest request;
        QString error;
        if (parseCourseLine(line, semester, request, error)) {
            courses_.append(request);
        } else {
            invalidLines.append(line.trimmed());
        }
    }
    return invalidLines;
}

void QuotaMonitor::setSemester(const QString& semester) {
    if (semester_ == semester) {
        return;
    }
    semester_ = semester;
    for (CourseRequest& c : courses_) {
        c.semester = semester;
    }
    // Semester change invalidates prior availability snapshots (different keys).
    previousAvailability_.clear();
    seenCourses_.clear();
}

void QuotaMonitor::setIntervalSeconds(int seconds) {
    intervalSeconds_ = qMax(kMinIntervalSeconds, seconds);
    timer_->setInterval(intervalSeconds_ * 1000);
}

void QuotaMonitor::start() {
    if (courses_.isEmpty()) {
        return;
    }
    timer_->start();
    emit monitoringStarted();
    pollAll();   // poll immediately, then on each interval
}

void QuotaMonitor::stop() {
    timer_->stop();
    emit monitoringStopped();
}

void QuotaMonitor::refreshNow() {
    pollAll();
    // If monitoring is active, realign the periodic timer so the next automatic
    // poll is a full interval after this manual refresh (avoids a poll landing
    // immediately after a refresh, and keeps periodic polling running).
    if (timer_->isActive()) {
        timer_->start();   // restart resets the interval countdown
    }
}

void QuotaMonitor::pollAll() {
    for (const CourseRequest& request : courses_) {
        client_->fetchQuota(request);
    }
}

void QuotaMonitor::onHtmlFetched(const CourseRequest& request, const QString& html) {
    CourseQuotaResult result = QuotaParser::parseQuotaHtml(html, request);
    handleResult(result);
}

void QuotaMonitor::onFetchFailed(const CourseRequest& request, const QString& error) {
    CourseQuotaResult result;
    result.request = request;
    result.courseLabel = request.label();
    result.fetchOk = false;
    result.errorMessage = error;
    handleResult(result);
}

void QuotaMonitor::handleResult(const CourseQuotaResult& result) {
    const QString key = courseKey(result.request);

    // Only consider availability transitions for successful fetches; a failed
    // fetch should not be treated as "became unavailable".
    if (result.fetchOk) {
        const bool wasSeen = seenCourses_.contains(key);
        const bool wasAvailable = previousAvailability_.value(key, false);
        const bool isAvailable = result.hasAnyAvailableSeat();

        if (wasSeen && !wasAvailable && isAvailable) {
            emit courseBecameAvailable(result);
        }

        seenCourses_.insert(key);
        previousAvailability_[key] = isAvailable;
    }

    emit resultReady(result);
}
