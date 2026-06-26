#include "parser.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

namespace {

// Strip HTML markup and entities from a cell/label so we are left with text.
QString cleanHtmlText(QString s) {
    s.replace("&nbsp;", " ");
    s.remove(QRegularExpression("<[^>]*>"));
    return s.simplified();
}

// Parse an integer out of a cleaned cell. Keeps only leading digits so values
// like "45 " or "45/48" still yield 45. Returns false if no digits are present.
bool parseIntCell(const QString& cell, int& out) {
    static const QRegularExpression digits(R"((\d+))");
    QRegularExpressionMatch m = digits.match(cell);
    if (!m.hasMatch()) {
        return false;
    }
    bool ok = false;
    int v = m.captured(1).toInt(&ok);
    if (!ok) {
        return false;
    }
    out = v;
    return true;
}

} // namespace

CourseQuotaResult QuotaParser::parseQuotaHtml(
    const QString& html,
    const CourseRequest& request)
{
    CourseQuotaResult result;
    result.request = request;
    result.courseLabel = request.label();

    // Normalize non-breaking spaces up front so downstream matching is simpler.
    QString normalized = html;
    normalized.replace("&nbsp;", " ");

    // Course label: the result page wraps it in <strong>...</strong>.
    static const QRegularExpression labelRe(
        R"(<strong[^>]*>(.*?)</strong>)",
        QRegularExpression::DotMatchesEverythingOption |
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch labelMatch = labelRe.match(normalized);
    if (labelMatch.hasMatch()) {
        QString label = cleanHtmlText(labelMatch.captured(1));
        if (!label.isEmpty()) {
            result.courseLabel = label;
        }
    }

    // Each quota data row carries class="schtd" (single or double quotes).
    static const QRegularExpression rowRe(
        R"(<tr[^>]*class\s*=\s*['"]schtd['"][^>]*>(.*?)</tr>)",
        QRegularExpression::DotMatchesEverythingOption |
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression cellRe(
        R"(<td[^>]*>(.*?)</td>)",
        QRegularExpression::DotMatchesEverythingOption |
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator rowIt = rowRe.globalMatch(normalized);
    while (rowIt.hasNext()) {
        QRegularExpressionMatch rowMatch = rowIt.next();
        const QString rowInner = rowMatch.captured(1);

        QStringList cells;
        QRegularExpressionMatchIterator cellIt = cellRe.globalMatch(rowInner);
        while (cellIt.hasNext()) {
            cells << cleanHtmlText(cellIt.next().captured(1));
        }

        // Need at least the first four cells: department, status, quota, current.
        if (cells.size() < 4) {
            continue;
        }

        QuotaRow row;
        row.department = cells.at(0);
        row.status = cells.at(1);

        int quota = 0;
        int current = 0;
        const bool quotaIsNumeric = parseIntCell(cells.at(2), quota);
        const bool currentIsNumeric = parseIntCell(cells.at(3), current);

        // The current cell is always a number on a real quota row. Require it;
        // if it is missing, this is a header/label row, not data — skip it.
        if (!currentIsNumeric) {
            continue;
        }
        row.current = current;

        if (quotaIsNumeric) {
            row.quota = quota;
        } else {
            // A non-numeric quota cell (e.g. "Unlimited") means an unrestricted
            // quota: there is always room. Keep the row and mark it unlimited.
            row.unlimited = true;
        }

        result.rows.append(row);
    }

    result.fetchOk = !result.rows.isEmpty();
    if (!result.fetchOk) {
        // The server returns this when the course/section is not offered in the
        // selected semester. Report it distinctly from a true parse failure so
        // the user knows to check the course or pick another semester.
        if (normalized.contains("No Such Course", Qt::CaseInsensitive)) {
            result.errorMessage = "Course not offered in this semester.";
        } else {
            result.errorMessage = "No quota rows found in response.";
        }
    }

    return result;
}
