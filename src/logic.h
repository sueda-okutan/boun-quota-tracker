#pragma once

#include <QObject>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include "models.h"
#include "network.h"

// Orchestration / monitoring. Owns the course list, semester, polling timer,
// network client and parser wiring, and availability transition tracking.
// Contains no UI and no raw HTTP.
class QuotaMonitor : public QObject {
    Q_OBJECT

public:
    explicit QuotaMonitor(QObject* parent = nullptr);

    // Parse one free-text line into a normalized CourseRequest.
    // Accepts "TK 221.01", "TK221.01", "TK 221 01", "cmpe150.2", etc.
    // `semester` is stored on the request (not sent to the endpoint).
    // Returns true on success; on failure `error` describes the problem.
    static bool parseCourseLine(
        const QString& line,
        const QString& semester,
        CourseRequest& out,
        QString& error);

    // Replace the monitored course set from free-text input (one per line).
    // Returns the list of lines that failed to parse (empty if all ok).
    QStringList setCoursesFromText(const QString& text, const QString& semester);

    void setSemester(const QString& semester);
    QString semester() const { return semester_; }

    // Polling interval in seconds; clamped to the minimum.
    void setIntervalSeconds(int seconds);
    int intervalSeconds() const { return intervalSeconds_; }

    static constexpr int kMinIntervalSeconds = 30;
    static constexpr int kDefaultIntervalSeconds = 60;

    bool isRunning() const { return timer_->isActive(); }
    int courseCount() const { return courses_.size(); }

    // Monitored course labels in the order they were entered. The UI uses this
    // to lay out table rows up front so the order stays stable regardless of the
    // order in which async network replies arrive.
    QStringList courseLabels() const {
        QStringList labels;
        labels.reserve(courses_.size());
        for (const CourseRequest& c : courses_) {
            labels << c.label();
        }
        return labels;
    }

public slots:
    void start();
    void stop();
    void refreshNow();   // one-shot poll of all courses

signals:
    // Emitted for every completed query (success or failure) so the UI can
    // update the corresponding row on each result.
    void resultReady(const CourseQuotaResult& result);

    // Emitted only on a full -> available transition after first observation.
    void courseBecameAvailable(const CourseQuotaResult& result);

    void monitoringStarted();
    void monitoringStopped();

private slots:
    void onHtmlFetched(const CourseRequest& request, const QString& html);
    void onFetchFailed(const CourseRequest& request, const QString& error);

private:
    void pollAll();
    void handleResult(const CourseQuotaResult& result);

    QuotaNetworkClient* client_;
    QTimer* timer_;

    QVector<CourseRequest> courses_;
    QString semester_;
    int intervalSeconds_ = kDefaultIntervalSeconds;

    QHash<QString, bool> previousAvailability_;
    QSet<QString> seenCourses_;
};
