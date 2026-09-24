#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QFrame>
#include <QtMath>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QWebSocket;
class QWebSocketServer;
class QJsonObject;

class MetricCard : public QWidget {
    Q_OBJECT
public:
    explicit MetricCard(const QString &title, const QString &unit,
                        const QColor &accent, QWidget *parent = nullptr);
    void setValue(double value, int precision = 1);
    void setTextValue(const QString &value);
    void setSubtitle(const QString &subtitle);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *m_valueLabel;
    QLabel *m_subtitleLabel;
    QColor m_accent;
};

class GaugeWidget : public QWidget {
    Q_OBJECT
public:
    explicit GaugeWidget(const QColor &accent, double maximum,
                         const QString &unit, QWidget *parent = nullptr);
    void setValue(double value, int precision = 0);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_accent;
    double m_maximum;
    double m_value = 0.0;
    int m_precision = 0;
    QString m_unit;
};

class BarChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit BarChartWidget(QWidget *parent = nullptr);
    void append(double value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_values;
};

class TelemetryChart : public QWidget {
    Q_OBJECT
public:
    explicit TelemetryChart(QWidget *parent = nullptr);
    void append(double temperature, double humidity);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_temperature;
    QVector<double> m_humidity;
    QVector<qint64> m_clock;
    // 横轴时间窗口：从 10 秒开始随数据积累动态增长，最大 90 秒
    static constexpr qint64 kMinWindowMs = 10 * 1000;             // 10 秒
    static constexpr qint64 kMaxWindowMs = 90 * 1000;             // 90 秒
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    QWidget *createSidebar();
    QWidget *createOverviewPage();
    QWidget *createDataPage();
    QWidget *createDevicePage();
    QWidget *createSettingsPage();
    QWidget *createPanel(const QString &title, QWidget *content = nullptr);
    QWidget *createAirQualityPanel(QWidget *parent = nullptr);
    QPushButton *createNavButton(const QString &text, const QString &glyph, int page);
    void applyTheme();
    void setPage(int index, QPushButton *source);
    void startServer();
    void stopServer();
    void updateConnectionState();
    void appendLog(const QString &text, const QString &level = QStringLiteral("INFO"));
    void handleMessage(QWebSocket *client, const QString &message);
    void parseSensorBlock(const QString &name, const QJsonObject &block);
    void updateMetric(const QString &sensor, const QString &field, double value, qint64 timestamp);
    void updateAirQuality(double ppb);
    void addTableRow(qint64 timestamp, const QString &sensor, const QString &field, const QString &value);
    QString localAddressSummary() const;
    void updateClock();
    void updateAirWarmupHint();

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
    QStackedWidget *m_pages = nullptr;
    QList<QPushButton *> m_navButtons;
    QLabel *m_connectionPill = nullptr;
    QLabel *m_connectionDetail = nullptr;
    QLabel *m_clockLabel = nullptr;
    QLabel *m_lastFrameLabel = nullptr;
    QLabel *m_frameCountLabel = nullptr;
    QLabel *m_rateLabel = nullptr;
    QLabel *m_motionLabel = nullptr;
    QLabel *m_motionDetail = nullptr;
    QLabel *m_accelLabel = nullptr;
    QLabel *m_gyroLabel = nullptr;
    QLabel *m_temperatureValue = nullptr;
    QLabel *m_humidityValue = nullptr;
    QLabel *m_airValue = nullptr;
    QLabel *m_airLevelBadge = nullptr;
    QLabel *m_airAdvice = nullptr;
    QLabel *m_airLevel = nullptr;
    QLabel *m_airWarmupHint = nullptr;
    QWidget *m_airScale = nullptr;
    QWidget *m_airScaleMarker = nullptr;
    QLabel *m_lightChannels = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QPushButton *m_serverButton = nullptr;
    QListWidget *m_logList = nullptr;
    QTableWidget *m_dataTable = nullptr;
    QTableWidget *m_overviewTable = nullptr;
    QListWidget *m_overviewEvents = nullptr;
    MetricCard *m_motionSummaryCard = nullptr;
    MetricCard *m_uptimeCard = nullptr;
    MetricCard *m_rateSummaryCard = nullptr;
    MetricCard *m_connectionSummaryCard = nullptr;
    GaugeWidget *m_soundGauge = nullptr;
    GaugeWidget *m_lightGauge = nullptr;
    BarChartWidget *m_airChart = nullptr;
    TelemetryChart *m_chart = nullptr;
    QTimer *m_clockTimer = nullptr;
    qint64 m_lastFrameAt = 0;
    qint64 m_rateWindowStart = 0;
    quint64 m_frameCount = 0;
    quint64 m_rateWindowFrames = 0;
    qint64 m_startedAt = 0;
    double m_latestTemperature = qQNaN();
    double m_latestHumidity = qQNaN();
    double m_light1 = qQNaN();
    double m_light2 = qQNaN();
};

#endif
