#pragma once

#include <QMainWindow>
#include <QHash>

#include "models.h"
#include "logic.h"

#ifndef __EMSCRIPTEN__
#include <QSystemTrayIcon>
#endif

class QTextEdit;
class QComboBox;
class QSpinBox;
class QPushButton;
class QTableWidget;
class QLabel;

// UI only. No parsing and no raw HTTP; everything goes through QuotaMonitor.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onRefreshClicked();
    void onSemesterChanged();
    void onIntervalChanged(int seconds);

    void onResultReady(const CourseQuotaResult& result);
    void onCourseBecameAvailable(const CourseQuotaResult& result);
    void onMonitoringStarted();
    void onMonitoringStopped();

private:
    void buildUi();
    void populateSemesters();
    bool applyCoursesFromInput();   // pushes textarea content into the monitor
    int rowForCourse(const QString& label);   // find-or-create table row
    void resetTitle();

    QuotaMonitor* monitor_;

    QTextEdit* courseInput_;
    QComboBox* semesterComboBox_;
    QSpinBox* intervalSpinBox_;
    QPushButton* startButton_;
    QPushButton* stopButton_;
    QPushButton* refreshButton_;
    QTableWidget* quotaTable_;
    QLabel* statusLabel_;

    // course label -> table row index
    QHash<QString, int> rowByCourse_;

#ifndef __EMSCRIPTEN__
    QSystemTrayIcon* trayIcon_ = nullptr;
#endif
};
