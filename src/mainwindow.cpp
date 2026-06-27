#include "mainwindow.h"

#include <QApplication>
#include <QColor>
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Custom item data role used to remember each cell's base (unselected) state
// color, so a selected row can be redrawn as a lighter version of it.
constexpr int kBaseColorRole = Qt::UserRole + 1;

enum Column {
    ColCourse = 0,
    ColDepartment,
    ColStatus,
    ColQuota,
    ColCurrent,
    ColAvailable,
    ColState,
    ColLastChecked,
    ColError,
    ColCount
};

const char* const kColumnNames[ColCount] = {
    "Course", "Department", "Status", "Quota", "Current",
    "Available", "State", "Last Checked", "Error"
};

QString nowTimeString() {
    return QTime::currentTime().toString("HH:mm:ss");
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , monitor_(new QuotaMonitor(this))
{
    buildUi();
    resetTitle();

    monitor_->setIntervalSeconds(intervalSpinBox_->value());

    connect(monitor_, &QuotaMonitor::resultReady,
            this, &MainWindow::onResultReady);
    connect(monitor_, &QuotaMonitor::courseBecameAvailable,
            this, &MainWindow::onCourseBecameAvailable);
    connect(monitor_, &QuotaMonitor::monitoringStarted,
            this, &MainWindow::onMonitoringStarted);
    connect(monitor_, &QuotaMonitor::monitoringStopped,
            this, &MainWindow::onMonitoringStopped);

#ifndef __EMSCRIPTEN__
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        trayIcon_ = new QSystemTrayIcon(this);
        trayIcon_->setIcon(windowIcon());
        trayIcon_->setToolTip("BOUN Quota Tracker");
        trayIcon_->show();
    }
#endif
}

void MainWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);

    // --- Input controls ---
    courseInput_ = new QTextEdit(central);
    courseInput_->setPlaceholderText(
        "One course per line, e.g.\nCMPE 150.02\nTK 221.01\nHTR 312.01");
    courseInput_->setMaximumHeight(120);
    mainLayout->addWidget(new QLabel("Courses:", central));
    mainLayout->addWidget(courseInput_);

    auto* controls = new QHBoxLayout();

    // Read-only: the server picks the active term; the app cannot change it.
    // Populated from the donem value parsed out of quota responses.
    controls->addWidget(new QLabel("Semester:", central));
    semesterLabel_ = new QLabel("—", central);
    controls->addWidget(semesterLabel_);

    controls->addWidget(new QLabel("Interval (s):", central));
    intervalSpinBox_ = new QSpinBox(central);
    intervalSpinBox_->setRange(QuotaMonitor::kMinIntervalSeconds, 3600);
    intervalSpinBox_->setValue(QuotaMonitor::kDefaultIntervalSeconds);
    intervalSpinBox_->setSingleStep(5);
    controls->addWidget(intervalSpinBox_);

    startButton_ = new QPushButton("Start", central);
    stopButton_ = new QPushButton("Stop", central);
    refreshButton_ = new QPushButton("Refresh now", central);
    stopButton_->setEnabled(false);
    controls->addWidget(startButton_);
    controls->addWidget(stopButton_);
    controls->addWidget(refreshButton_);
    controls->addStretch();

    mainLayout->addLayout(controls);

    // --- Quota table ---
    quotaTable_ = new QTableWidget(0, ColCount, central);
    QStringList headers;
    for (int i = 0; i < ColCount; ++i) {
        headers << kColumnNames[i];
    }
    quotaTable_->setHorizontalHeaderLabels(headers);
    quotaTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // Don't bold/highlight the header section of the selected column or row.
    quotaTable_->horizontalHeader()->setHighlightSections(false);
    quotaTable_->verticalHeader()->setHighlightSections(false);
    // Wrap long cell text (e.g. the Error column) and let row height grow to fit
    // it, so error messages are fully readable. Hovering also shows a tooltip.
    quotaTable_->setWordWrap(true);
    quotaTable_->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    quotaTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    quotaTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Disable Qt's default opaque-blue selection highlight. We re-tint the
    // selected row ourselves to a lighter version of its own state color
    // (see onSelectionChanged). Keep the normal text color, not white.
    quotaTable_->setStyleSheet(
        "QTableWidget::item:selected {"
        "  background: transparent;"
        "  color: palette(text);"
        "}");
    connect(quotaTable_, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::onSelectionChanged);
    mainLayout->addWidget(quotaTable_);

    // --- Status ---
    statusLabel_ = new QLabel("Idle.", central);
    mainLayout->addWidget(statusLabel_);

    setCentralWidget(central);
    resize(1000, 600);

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(intervalSpinBox_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::onIntervalChanged);
}

void MainWindow::resetTitle() {
    setWindowTitle("BOUN Quota Tracker");
}

bool MainWindow::applyCoursesFromInput() {
    // No semester is chosen by the user; the server uses its own active term.
    const QStringList invalid =
        monitor_->setCoursesFromText(courseInput_->toPlainText(), QString());

    // Rebuild the table from scratch, pre-creating one row per course in the
    // order they were entered. Doing this up front (rather than lazily as
    // results arrive) keeps the row order stable: async network replies can
    // finish in any order, but the layout is already fixed.
    rowByCourse_.clear();
    quotaTable_->setRowCount(0);
    for (const QString& label : monitor_->courseLabels()) {
        rowForCourse(label);
    }

    if (!invalid.isEmpty()) {
        statusLabel_->setText(
            "Ignored invalid line(s): " + invalid.join(", "));
    }

    if (monitor_->courseCount() == 0) {
        statusLabel_->setText("No valid courses to monitor.");
        return false;
    }
    return true;
}

int MainWindow::rowForCourse(const QString& label) {
    auto it = rowByCourse_.find(label);
    if (it != rowByCourse_.end()) {
        return it.value();
    }
    const int row = quotaTable_->rowCount();
    quotaTable_->insertRow(row);
    for (int c = 0; c < ColCount; ++c) {
        quotaTable_->setItem(row, c, new QTableWidgetItem());
    }
    quotaTable_->item(row, ColCourse)->setText(label);
    rowByCourse_.insert(label, row);
    return row;
}

void MainWindow::onStartClicked() {
    if (!applyCoursesFromInput()) {
        return;
    }
    monitor_->start();
}

void MainWindow::onStopClicked() {
    monitor_->stop();
}

void MainWindow::onRefreshClicked() {
    // While monitoring, a manual refresh should poll the courses already being
    // tracked WITHOUT rebuilding them: re-applying the input would wipe the
    // availability history (breaking notifications) and rebuild the table. Only
    // (re)apply the input when not currently monitoring.
    if (!monitor_->isRunning()) {
        if (!applyCoursesFromInput()) {
            return;
        }
    }
    statusLabel_->setText("Refreshing...");
    monitor_->refreshNow();

    // Restore the monitoring status line so the bar doesn't get stuck on
    // "Refreshing..." while periodic polling is still active.
    if (monitor_->isRunning()) {
        onMonitoringStarted();
    }
}

void MainWindow::onIntervalChanged(int seconds) {
    monitor_->setIntervalSeconds(seconds);
}

void MainWindow::onMonitoringStarted() {
    startButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    statusLabel_->setText(
        QString("Monitoring %1 course(s) every %2s.")
            .arg(monitor_->courseCount())
            .arg(monitor_->intervalSeconds()));
}

void MainWindow::onMonitoringStopped() {
    startButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    statusLabel_->setText("Stopped.");
}

void MainWindow::onResultReady(const CourseQuotaResult& result) {
    const QString label = result.request.label();
    const int row = rowForCourse(label);

    // Show the server's active term (parsed from the response) once we know it.
    if (!result.semester.isEmpty()) {
        semesterLabel_->setText(result.semester);
    }

    QString department;
    QString status;
    QString quota;
    QString current;
    QString available;
    QString state;

    if (!result.fetchOk) {
        state = "ERROR";
        QTableWidgetItem* errorItem = quotaTable_->item(row, ColError);
        errorItem->setText(result.errorMessage);
        // Full message on hover, since the column may be too narrow to show it.
        errorItem->setToolTip(result.errorMessage);
    } else {
        // Aggregate across rows: sum quota/current, available = quota - current.
        int totalQuota = 0;
        int totalCurrent = 0;
        bool anyUnlimited = false;
        QStringList departments;
        QStringList statuses;
        for (const QuotaRow& r : result.rows) {
            if (r.unlimited) {
                anyUnlimited = true;
            } else {
                totalQuota += r.quota;
            }
            totalCurrent += r.current;
            if (!r.department.isEmpty()) departments << r.department;
            if (!r.status.isEmpty()) statuses << r.status;
        }
        department = departments.join(", ");
        status = statuses.join(", ");
        // An unlimited quota has no finite seat count, so show "Unlimited"
        // instead of a misleading aggregate number.
        quota = anyUnlimited ? "Unlimited" : QString::number(totalQuota);
        current = QString::number(totalCurrent);
        available = anyUnlimited ? "Unlimited"
                                 : QString::number(result.totalAvailableSeats());
        state = result.hasAnyAvailableSeat() ? "AVAILABLE" : "FULL";
        QTableWidgetItem* errorItem = quotaTable_->item(row, ColError);
        errorItem->setText("");
        errorItem->setToolTip("");   // clear any stale error tooltip
    }

    quotaTable_->item(row, ColCourse)->setText(result.courseLabel.isEmpty()
                                                   ? label
                                                   : result.courseLabel);
    quotaTable_->item(row, ColDepartment)->setText(department);
    quotaTable_->item(row, ColStatus)->setText(status);
    quotaTable_->item(row, ColQuota)->setText(quota);
    quotaTable_->item(row, ColCurrent)->setText(current);
    quotaTable_->item(row, ColAvailable)->setText(available);
    quotaTable_->item(row, ColState)->setText(state);
    quotaTable_->item(row, ColLastChecked)->setText(nowTimeString());

    // Row coloring reflects state.
    QColor bg;
    if (state == "AVAILABLE") {
        bg = QColor(15, 220, 20);   // green
    } else if (state == "ERROR") {
        bg = QColor(240, 185, 35);   // amber
    } else if (state == "FULL") {
        bg = QColor(200, 45, 35);   // red
    }
    // Remember the base color on each cell (in a custom role) and paint the row,
    // accounting for whether it is currently selected.
    for (int c = 0; c < ColCount; ++c) {
        quotaTable_->item(row, c)->setData(kBaseColorRole, bg);
    }
    applyRowColor(row);
}

void MainWindow::applyRowColor(int row) {
    if (row < 0 || row >= quotaTable_->rowCount()) {
        return;
    }
    const bool selected = quotaTable_->selectionModel()->isRowSelected(
        row, QModelIndex());
    for (int c = 0; c < ColCount; ++c) {
        QTableWidgetItem* item = quotaTable_->item(row, c);
        if (!item) {
            continue;
        }
        const QColor base = item->data(kBaseColorRole).value<QColor>();
        if (!base.isValid()) {
            // No state color for this row: clear any brush so it uses the
            // default background.
            item->setData(Qt::BackgroundRole, QVariant());
            continue;
        }
        // Selected rows get a lighter version of the same color.
        item->setBackground(selected ? base.lighter(140) : base);
    }
}

void MainWindow::onSelectionChanged() {
    // Repaint every row so the previously selected row reverts to its base color
    // and the newly selected one is lightened.
    for (int row = 0; row < quotaTable_->rowCount(); ++row) {
        applyRowColor(row);
    }
}

void MainWindow::onCourseBecameAvailable(const CourseQuotaResult& result) {
    const QString label = result.courseLabel.isEmpty()
                              ? result.request.label()
                              : result.courseLabel;
    const QString message =
        QString("Seat available: %1 (%2 seat(s))")
            .arg(label)
            .arg(result.totalAvailableSeats());

    statusLabel_->setText(message);
    setWindowTitle("✅ " + message + " — BOUN Quota Tracker");
    QApplication::beep();

    // Briefly emphasize the row by selecting it.
    const int row = rowForCourse(result.request.label());
    quotaTable_->selectRow(row);

#ifndef __EMSCRIPTEN__
    if (trayIcon_) {
        trayIcon_->showMessage(
            "BOUN Quota Tracker",
            message,
            QSystemTrayIcon::Information,
            10000);
    }
#endif
}
