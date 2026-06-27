#pragma once

#include <QMainWindow>
#include <QHash>

#include "models.h"
#include "logic.h"

#ifndef __EMSCRIPTEN__
#include <QSystemTrayIcon>
#endif

class QTextEdit;
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
    void onIntervalChanged(int seconds);

    void onResultReady(const CourseQuotaResult& result);
    void onCourseBecameAvailable(const CourseQuotaResult& result);
    void onMonitoringStarted();
    void onMonitoringStopped();
    void onSelectionChanged();   // re-tint selected row to a lighter shade

private:
    void buildUi();
    bool applyCoursesFromInput();   // pushes textarea content into the monitor
    int rowForCourse(const QString& label);   // find-or-create table row
    void applyRowColor(int row);   // paint base or lighter (if selected) color
    void resetTitle();

    QuotaMonitor* monitor_;

    QTextEdit* courseInput_;
    QLabel* semesterLabel_;   // read-only; shows the server's active term
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
