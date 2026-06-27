#pragma once

#include "models.h"
#include <QString>

// HTML parsing logic.
class QuotaParser {
public:
    // Parse the raw HTML body returned by POST /scripts/quotasearch.asp into CourseQuotaResult.
    static CourseQuotaResult parseQuotaHtml(
        const QString& html,
        const CourseRequest& request
    );
};
