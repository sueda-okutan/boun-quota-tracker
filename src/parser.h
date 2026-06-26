#pragma once

#include "models.h"
#include <QString>

// HTML parsing logic only. No UI, network, or timer code.
class QuotaParser {
public:
    // Parse the raw HTML body returned by POST /scripts/quotasearch.asp into a
    // CourseQuotaResult. Pure function: never touches the network.
    static CourseQuotaResult parseQuotaHtml(
        const QString& html,
        const CourseRequest& request
    );
};
