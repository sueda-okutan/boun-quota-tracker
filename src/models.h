#pragma once

#include <QString>
#include <QVector>

// Shared data structures only. No UI, network, parser, or timer code lives here.

struct CourseRequest {
    QString departmentCode;   // "TK", "HTR", "CMPE" (uppercased for UI/model)
    QString courseNumber;     // "221", "312", "150"
    QString section;          // "01", "02" (two digits)
    QString semester;         // UI/future compatibility; NOT sent to the confirmed POST endpoint

    QString courseCode() const {
        return departmentCode + " " + courseNumber;
    }

    QString label() const {
        return departmentCode + " " + courseNumber + "." + section;
    }
};

struct QuotaRow {
    QString department;
    QString status;
    int quota = 0;
    int current = 0;
    bool unlimited = false;   // quota cell was non-numeric (e.g. "Unlimited")

    // For an unlimited quota there is always room, so report a sentinel of -1
    // rather than a misleading negative/zero numeric difference.
    int availableSeats() const {
        if (unlimited) {
            return -1;
        }
        return quota - current;
    }

    bool hasAvailableSeat() const {
        if (unlimited) {
            return true;
        }
        return availableSeats() > 0;
    }
};

struct CourseQuotaResult {
    CourseRequest request;
    QString courseLabel;
    QVector<QuotaRow> rows;
    bool fetchOk = false;
    QString errorMessage;

    int totalAvailableSeats() const {
        int total = 0;
        for (const auto& row : rows) {
            if (row.availableSeats() > 0) {
                total += row.availableSeats();
            }
        }
        return total;
    }

    bool hasAnyAvailableSeat() const {
        for (const auto& row : rows) {
            if (row.hasAvailableSeat()) {
                return true;
            }
        }
        return false;
    }
};

inline QString courseKey(const CourseRequest& request) {
    return request.label() + "@" + request.semester;
}
