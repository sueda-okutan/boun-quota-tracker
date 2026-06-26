#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include "models.h"

// HTTP logic only. No parsing and no UI.
class QuotaNetworkClient : public QObject {
    Q_OBJECT

public:
    explicit QuotaNetworkClient(QObject* parent = nullptr);

    // POST the quota query for `request` to the confirmed endpoint. Emits either
    // quotaHtmlFetched (raw response body) or quotaFetchFailed.
    void fetchQuota(const CourseRequest& request);

    // Exposed for tests: builds the confirmed POST body
    // "abbr=<lower>&code=<num>&section=<NN>". No semester is included.
    QByteArray buildPostBody(const CourseRequest& request) const;

signals:
    void quotaHtmlFetched(const CourseRequest& request, const QString& html);
    void quotaFetchFailed(const CourseRequest& request, const QString& error);

private:
    QUrl quotaEndpoint() const;
    QNetworkRequest buildRequest() const;

private:
    QNetworkAccessManager* networkManager;
};
