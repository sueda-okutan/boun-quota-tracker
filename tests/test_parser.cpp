#include <QtTest>

#include "models.h"
#include "parser.h"
#include "network.h"
#include "logic.h"

namespace {

CourseRequest makeRequest(const QString& dept, const QString& num,
                          const QString& sec, const QString& sem = QString()) {
    CourseRequest r;
    r.departmentCode = dept;
    r.courseNumber = num;
    r.section = sec;
    r.semester = sem;
    return r;
}

} // namespace

class TestParser : public QObject {
    Q_OBJECT

private slots:
    // ---- Parser ----
    void fullCourse();
    void availableCourse();
    void multipleRows();
    void noRows();
    void realResponseHtml();
    void unlimitedQuota();
    void noSuchCourse();
    void semesterParsed();
    void stripsInnerMarkup();

    // ---- POST body ----
    void postBodyTk();
    void postBodyCmpe();
    void postBodyNoSemester();

    // ---- Course line parsing / normalization ----
    void courseLineVariants();
    void courseLineInvalid();

    // ---- Availability transition ----
    void availabilityRule();
};

void TestParser::fullCourse() {
    const QString html =
        "<TITLE>Quota Information</TITLE>"
        "<strong>HTR 312.01</strong>"
        "<tr class='schtd'>"
        "<td>ALL&nbsp;</td><td>ALL&nbsp;</td><td>45&nbsp;</td><td>45&nbsp;</td>"
        "</tr>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("HTR", "312", "01"));

    QVERIFY(result.fetchOk);
    QCOMPARE(result.rows.size(), 1);
    QCOMPARE(result.rows[0].quota, 45);
    QCOMPARE(result.rows[0].current, 45);
    QVERIFY(!result.hasAnyAvailableSeat());
    QCOMPARE(result.totalAvailableSeats(), 0);
    QCOMPARE(result.courseLabel, QString("HTR 312.01"));
}

void TestParser::availableCourse() {
    const QString html =
        "<strong>CMPE 150.02</strong>"
        "<tr class='schtd'>"
        "<td>ALL&nbsp;</td><td>ALL&nbsp;</td><td>45&nbsp;</td><td>44&nbsp;</td>"
        "</tr>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("CMPE", "150", "02"));

    QVERIFY(result.fetchOk);
    QCOMPARE(result.rows.size(), 1);
    QVERIFY(result.hasAnyAvailableSeat());
    QCOMPARE(result.totalAvailableSeats(), 1);
}

void TestParser::multipleRows() {
    const QString html =
        "<strong>FOO 100.01</strong>"
        "<tr class='schtd'><td>ALL</td><td>ALL</td><td>30</td><td>30</td></tr>"
        "<tr class=\"schtd\"><td>CMPE</td><td>RESTRICTED</td><td>10</td><td>5</td></tr>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("FOO", "100", "01"));

    QVERIFY(result.fetchOk);
    QCOMPARE(result.rows.size(), 2);
    // Second row has 5 free seats; overall available.
    QVERIFY(result.hasAnyAvailableSeat());
    QCOMPARE(result.totalAvailableSeats(), 5);
    QCOMPARE(result.rows[1].department, QString("CMPE"));
    QCOMPARE(result.rows[1].status, QString("RESTRICTED"));
}

void TestParser::noRows() {
    const QString html =
        "<TITLE>Quota Information</TITLE>"
        "<strong>BAD 999.99</strong>"
        "<p>No quota information available.</p>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("BAD", "999", "99"));

    QVERIFY(!result.fetchOk);
    QCOMPARE(result.rows.size(), 0);
    QVERIFY(!result.errorMessage.isEmpty());
}

void TestParser::realResponseHtml() {
    // Exact shape captured from a live POST /scripts/quotasearch.asp response,
    // including the <p align=center> markup inside each <td>.
    const QString html = R"HTML(
<HTML><HEAD><TITLE>Quota Information</TITLE></HEAD><BODY>
<font class="bodytextdark12"><strong>HTR 312.01</strong></font>
<br><b>Max. Classroom Capacity:</b> 48<br><br>
<table>
<tr class="title">
    <td><p align=center>Department</td><td><p align=center>Statu</td>
    <td><p align=center>Quota</td><td><p align=center>Current</td>
</tr>
<tr class='schtd'><td width='45%' ><p align=center>ALL&nbsp;</td><td width='20%'><p align=center>ALL&nbsp;</td><td width='10%'><p align=center>45&nbsp;</td><td width='10%'><p align=center>45&nbsp;</td></tr>
</table>
</BODY></HTML>
)HTML";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("HTR", "312", "01"));

    QVERIFY(result.fetchOk);
    QCOMPARE(result.rows.size(), 1);
    QCOMPARE(result.rows[0].department, QString("ALL"));
    QCOMPARE(result.rows[0].status, QString("ALL"));
    QCOMPARE(result.rows[0].quota, 45);
    QCOMPARE(result.rows[0].current, 45);
    QVERIFY(!result.hasAnyAvailableSeat());
    QCOMPARE(result.courseLabel, QString("HTR 312.01"));
}

void TestParser::unlimitedQuota() {
    // Exact row shape from a live response when the quota is unrestricted: the
    // Quota cell reads "Unlimited" while Current is still numeric.
    const QString html = R"HTML(
<strong>UNL 101.01</strong>
<tr class='schtd'><td width='45%' ><p align=center>ALL&nbsp;</td><td width='20%'><p align=center>ALL&nbsp;</td><td width='10%'><p align=center>Unlimited&nbsp;</td><td width='10%'><p align=center>102&nbsp;</td></tr>
)HTML";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("UNL", "101", "01"));

    QVERIFY(result.fetchOk);
    QCOMPARE(result.rows.size(), 1);
    QVERIFY(result.rows[0].unlimited);
    QCOMPARE(result.rows[0].current, 102);
    // Unlimited means always available, and must not count as an ERROR/full row.
    QVERIFY(result.rows[0].hasAvailableSeat());
    QVERIFY(result.hasAnyAvailableSeat());
    // The -1 sentinel must not leak into the finite seat total.
    QCOMPARE(result.totalAvailableSeats(), 0);
}

void TestParser::noSuchCourse() {
    // Exact body returned by the live endpoint when the course/section is not
    // offered in the active semester.
    const QString html =
        "<HTML><HEAD><TITLE>Quota Information</TITLE></HEAD><BODY>"
        "<!-- select ders,section ... where ders='cmpe150' and section='01' -->"
        "<center>No Such Course In This Semester...</center>"
        "</BODY></HTML>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("CMPE", "150", "01"));

    QVERIFY(!result.fetchOk);
    QCOMPARE(result.rows.size(), 0);
    QCOMPARE(result.errorMessage, QString("Course not offered in this semester."));
}

void TestParser::semesterParsed() {
    // The server echoes its active term in an HTML comment as donem='YYYY/YYYY-T'.
    // The parser must extract it into result.semester (this is the only source
    // of the current semester for the UI).
    const QString html =
        "<strong>HTR 312.01</strong>"
        "<!-- ... where ders='HTR  312' and section='01' and donem='2025/2026-3' -->"
        "<tr class='schtd'><td>ALL</td><td>ALL</td><td>45</td><td>45</td></tr>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("HTR", "312", "01"));

    QCOMPARE(result.semester, QString("2025/2026-3"));

    // It is also present on a "No Such Course" response, so the UI can still show
    // the active term even when the queried course is not offered.
    const QString notOffered =
        "<!-- where ders='ie  495' and donem='2025/2026-3' -->"
        "<center>No Such Course In This Semester...</center>";
    CourseQuotaResult r2 =
        QuotaParser::parseQuotaHtml(notOffered, makeRequest("IE", "495", "01"));
    QCOMPARE(r2.semester, QString("2025/2026-3"));
}

void TestParser::stripsInnerMarkup() {
    // The "title" row (class="title") must not be picked up as a data row.
    const QString html =
        "<strong>X 100.01</strong>"
        "<tr class='title'><td>Department</td><td>Statu</td><td>Quota</td><td>Current</td></tr>"
        "<tr class='schtd'><td><b>ALL</b></td><td>ALL</td><td>50</td><td>10</td></tr>";

    CourseQuotaResult result =
        QuotaParser::parseQuotaHtml(html, makeRequest("X", "100", "01"));

    QCOMPARE(result.rows.size(), 1);
    QCOMPARE(result.rows[0].department, QString("ALL"));
    QCOMPARE(result.rows[0].quota, 50);
    QCOMPARE(result.rows[0].current, 10);
}

void TestParser::postBodyTk() {
    // abbr is sent UPPERCASE: the server's department code is case-sensitive.
    // Lowercase ("ie") returns "No Such Course" for some departments; uppercase
    // works for all. Verified against the live endpoint.
    QuotaNetworkClient client;
    QByteArray body = client.buildPostBody(makeRequest("TK", "221", "01"));
    QCOMPARE(QString::fromUtf8(body), QString("abbr=TK&code=221&section=01"));
}

void TestParser::postBodyCmpe() {
    QuotaNetworkClient client;
    QByteArray body = client.buildPostBody(makeRequest("CMPE", "150", "02"));
    QCOMPARE(QString::fromUtf8(body), QString("abbr=CMPE&code=150&section=02"));

    // A lowercase-typed department must still be sent uppercase.
    QByteArray ie = client.buildPostBody(makeRequest("ie", "495", "01"));
    QCOMPARE(QString::fromUtf8(ie), QString("abbr=IE&code=495&section=01"));
}

void TestParser::postBodyNoSemester() {
    QuotaNetworkClient client;
    QByteArray body =
        client.buildPostBody(makeRequest("HTR", "312", "01", "2025/2026-3"));
    const QString s = QString::fromUtf8(body);
    QVERIFY(!s.contains("semester"));
    QVERIFY(!s.contains("donem"));
    QVERIFY(!s.contains("2025"));
}

void TestParser::courseLineVariants() {
    struct Case {
        QString input;
        QString dept;
        QString num;
        QString sec;
    };
    const QVector<Case> cases = {
        {"TK 221.01", "TK", "221", "01"},
        {"TK221.01", "TK", "221", "01"},
        {"TK 221 01", "TK", "221", "01"},
        {"HTR 312.01", "HTR", "312", "01"},
        {"CMPE 150.02", "CMPE", "150", "02"},
        {"cmpe150.2", "CMPE", "150", "02"},
        {"tk 221 1", "TK", "221", "01"},
        {"  cmpe 150.2  ", "CMPE", "150", "02"},
    };

    for (const Case& c : cases) {
        CourseRequest out;
        QString error;
        const bool ok =
            QuotaMonitor::parseCourseLine(c.input, "2025/2026-3", out, error);
        QVERIFY2(ok, qPrintable("failed to parse: " + c.input + " (" + error + ")"));
        QCOMPARE(out.departmentCode, c.dept);
        QCOMPARE(out.courseNumber, c.num);
        QCOMPARE(out.section, c.sec);
        QCOMPARE(out.semester, QString("2025/2026-3"));
    }
}

void TestParser::courseLineInvalid() {
    const QStringList bad = {"", "   ", "TK", "TK 22.01", "garbage", "123 456"};
    for (const QString& line : bad) {
        CourseRequest out;
        QString error;
        const bool ok = QuotaMonitor::parseCourseLine(line, "", out, error);
        QVERIFY2(!ok, qPrintable("expected failure for: \"" + line + "\""));
        QVERIFY(!error.isEmpty());
    }
}

void TestParser::availabilityRule() {
    // quota > current  =>  available
    QuotaRow full;
    full.quota = 45;
    full.current = 45;
    QVERIFY(!full.hasAvailableSeat());
    QCOMPARE(full.availableSeats(), 0);

    QuotaRow open;
    open.quota = 45;
    open.current = 44;
    QVERIFY(open.hasAvailableSeat());
    QCOMPARE(open.availableSeats(), 1);

    QuotaRow over;   // current somehow exceeds quota
    over.quota = 45;
    over.current = 50;
    QVERIFY(!over.hasAvailableSeat());
}

QTEST_MAIN(TestParser)
#include "test_parser.moc"
