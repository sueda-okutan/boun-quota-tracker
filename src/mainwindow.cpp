#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDate>
#include <QHBoxLayout>
#include <QHeaderView>
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
    populateSemesters();
    resetTitle();

    monitor_->setIntervalSeconds(intervalSpinBox_->value());
    monitor_->setSemester(semesterComboBox_->currentText());

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

    controls->addWidget(new QLabel("Semester:", central));
    semesterComboBox_ = new QComboBox(central);
    controls->addWidget(semesterComboBox_);

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
    quotaTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    quotaTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(quotaTable_);

    // --- Status ---
    statusLabel_ = new QLabel("Idle.", central);
    mainLayout->addWidget(statusLabel_);

    setCentralWidget(central);
    resize(1000, 600);

    connect(startButton_, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(refreshButton_, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(semesterComboBox_, &QComboBox::currentTextChanged,
            this, &MainWindow::onSemesterChanged);
    connect(intervalSpinBox_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::onIntervalChanged);
}

void MainWindow::populateSemesters() {
    // Build a handful of recent academic years in YYYY/YYYY-T format. The
    // selected value is stored on each CourseRequest but is NOT submitted to the
    // confirmed quota endpoint.
    const int currentYear = QDate::currentDate().year();

    // Offer the upcoming year first, then walk back two years.
    for (int startYear = currentYear; startYear >= currentYear - 2; --startYear) {
        for (int term = 3; term >= 1; --term) {
            const QString value = QString("%1/%2-%3")
                                      .arg(startYear)
                                      .arg(startYear + 1)
                                      .arg(term);
            semesterComboBox_->addItem(value);
        }
    }
}

void MainWindow::resetTitle() {
    setWindowTitle("BOUN Quota Tracker");
}

bool MainWindow::applyCoursesFromInput() {
    const QStringList invalid =
        monitor_->setCoursesFromText(courseInput_->toPlainText(),
                                     semesterComboBox_->currentText());

    // Rebuild the table mapping; results will repopulate rows as they arrive.
    rowByCourse_.clear();
    quotaTable_->setRowCount(0);

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
    if (!applyCoursesFromInput()) {
        return;
    }
    statusLabel_->setText("Refreshing...");
    monitor_->refreshNow();
}

void MainWindow::onSemesterChanged() {
    monitor_->setSemester(semesterComboBox_->currentText());
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

    QString department;
    QString status;
    QString quota;
    QString current;
    QString available;
    QString state;

    if (!result.fetchOk) {
        state = "ERROR";
        quotaTable_->item(row, ColError)->setText(result.errorMessage);
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
        quotaTable_->item(row, ColError)->setText("");
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
        bg = QColor(198, 239, 206);   // green
    } else if (state == "ERROR") {
        bg = QColor(255, 235, 156);   // amber
    } else if (state == "FULL") {
        bg = QColor(255, 199, 206);   // red
    }
    if (bg.isValid()) {
        for (int c = 0; c < ColCount; ++c) {
            quotaTable_->item(row, c)->setBackground(bg);
        }
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
