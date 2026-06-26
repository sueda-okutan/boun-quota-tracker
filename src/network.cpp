#include "network.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTimer>

namespace {
// Abort a request that has not finished within this many milliseconds.
constexpr int kRequestTimeoutMs = 10000;

// Decode the raw response bytes using the charset the server/page declares,
// falling back through a sensible chain. The BOUN quota page is iso8859-9
// (Latin-5), so forcing UTF-8 would corrupt Turkish characters.
QString decodeBody(const QByteArray& raw, QNetworkReply* reply) {
    // 1) Honour the HTTP Content-Type charset if present.
    const QString contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toString();
    int idx = contentType.toLower().indexOf("charset=");
    QString charset;
    if (idx >= 0) {
        charset = contentType.mid(idx + 8).trimmed();
    }

    // 2) Otherwise look for a <meta ... charset=...> in the first bytes.
    if (charset.isEmpty()) {
        const QString head = QString::fromLatin1(raw.left(1024)).toLower();
        int m = head.indexOf("charset=");
        if (m >= 0) {
            charset = head.mid(m + 8);
            charset = charset.left(charset.indexOf(QRegularExpression("[\"'>;\\s]")));
            charset = charset.trimmed();
        }
    }

    if (!charset.isEmpty()) {
        if (auto enc = QStringConverter::encodingForName(charset.toUtf8().constData())) {
            return QStringDecoder(*enc).decode(raw);
        }
        // iso8859-9 maps to Latin-5; Qt's named-codec path covers it via ICU,
        // but fall back to Latin1 if the name is unknown to the build.
        if (charset.contains("8859-9") || charset.contains("latin5")) {
            return QString::fromLatin1(raw);
        }
    }

    // 3) Last resort: UTF-8 (valid ASCII passes through unchanged).
    return QString::fromUtf8(raw);
}

// Write the most recent raw response to a debug file so "no rows" reports can
// be diagnosed against exactly what BOUN returned. Best-effort; ignores errors.
void dumpRawResponse(const CourseRequest& request, const QByteArray& raw) {
    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (dir.isEmpty()) {
        return;
    }
    QFile f(QDir(dir).filePath("boun_quota_last_response.html"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("<!-- " + request.label().toUtf8() + " -->\n");
        f.write(raw);
        f.close();
    }
}
} // namespace

QuotaNetworkClient::QuotaNetworkClient(QObject* parent)
    : QObject(parent)
    , networkManager(new QNetworkAccessManager(this))
{
}

QUrl QuotaNetworkClient::quotaEndpoint() const {
    return QUrl("https://registration.boun.edu.tr/scripts/quotasearch.asp");
}

QNetworkRequest QuotaNetworkClient::buildRequest() const {
    QNetworkRequest request(quotaEndpoint());

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        "application/x-www-form-urlencoded");

    request.setRawHeader("Origin", "https://registration.boun.edu.tr");
    request.setRawHeader("Referer", "https://registration.boun.edu.tr/quotaentry.htm");
    request.setRawHeader("User-Agent", "BOUN-Quota-Tracker/1.0 Qt");
    request.setRawHeader(
        "Accept",
        "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");

    // quotasearch.asp may 302-redirect to the page that actually renders the
    // quota table. Without this, Qt returns the (empty) redirect body and the
    // parser sees no rows.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    return request;
}

QByteArray QuotaNetworkClient::buildPostBody(const CourseRequest& request) const {
    QUrlQuery body;
    // The server's stored department code is UPPERCASE and case-sensitive.
    // Lowercase happens to match some departments (TK, HTR) but NOT others
    // (e.g. IE), which return "No Such Course". Verified against the live
    // endpoint: abbr=IE returns rows, abbr=ie does not. Always send uppercase.
    body.addQueryItem("abbr", request.departmentCode.toUpper());
    body.addQueryItem("code", request.courseNumber);
    body.addQueryItem("section", request.section);
    return body.query(QUrl::FullyEncoded).toUtf8();
}

void QuotaNetworkClient::fetchQuota(const CourseRequest& request) {
    QNetworkRequest requestObj = buildRequest();
    QByteArray body = buildPostBody(request);

    QNetworkReply* reply = networkManager->post(requestObj, body);

    // Timeout guard: abort the reply if it stalls. The finished handler still
    // runs afterwards (with an OperationCanceledError) and reports the failure.
    QTimer* timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timeoutTimer->start(kRequestTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        reply->deleteLater();

        QVariant statusCode =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        if (reply->error() != QNetworkReply::NoError) {
#ifdef __EMSCRIPTEN__
            emit quotaFetchFailed(
                request,
                "Network request failed. In WebAssembly/GitHub Pages, this may be "
                "caused by CORS restrictions. Use the native app or a trusted "
                "backend/proxy.");
#else
            emit quotaFetchFailed(
                request,
                "Network request failed: " + reply->errorString());
#endif
            return;
        }

        if (statusCode.isValid() && statusCode.toInt() != 200) {
            emit quotaFetchFailed(
                request,
                "HTTP error: " + QString::number(statusCode.toInt()));
            return;
        }

        const QByteArray raw = reply->readAll();
        const QString html = decodeBody(raw, reply);

        if (html.trimmed().isEmpty()) {
            // Surface enough detail to tell an empty body apart from a redirect
            // or a charset problem when diagnosing "no rows" reports.
            const int status = statusCode.isValid() ? statusCode.toInt() : -1;
            const QUrl finalUrl =
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            emit quotaFetchFailed(
                request,
                QString("Empty quota response (HTTP %1, %2 bytes%3).")
                    .arg(status)
                    .arg(raw.size())
                    .arg(finalUrl.isEmpty()
                             ? QString()
                             : ", redirect -> " + finalUrl.toString()));
            return;
        }

        dumpRawResponse(request, raw);
        emit quotaHtmlFetched(request, html);
    });
}
