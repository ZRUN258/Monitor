#include "mainwindow.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkInterface>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtWebSockets/QWebSocket>
#include <QtWebSockets/QWebSocketServer>

namespace {
constexpr int kMaxChartPoints = 90;
constexpr int kMaxLogRows = 120;
constexpr int kMaxTableRows = 500;

QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("muted"));
    return label;
}

QString displayTime(qint64 timestamp) {
    if (timestamp <= 0) return QStringLiteral("--:--:--");
    return QDateTime::fromMSecsSinceEpoch(timestamp).toString(QStringLiteral("HH:mm:ss.zzz"));
}

double jsonNumber(const QJsonValue &value) {
    if (value.isDouble()) return value.toDouble();
    if (value.isBool()) return value.toBool() ? 1.0 : 0.0;
    if (value.isString()) {
        bool ok = false;
        const double number = value.toString().toDouble(&ok);
        if (ok) return number;
    }
    return 0.0;
}

QStringList defaultFieldsForSensor(const QString &sensor) {
    if (sensor == QStringLiteral("dht"))
        return {QStringLiteral("temperature"), QStringLiteral("humidity")};
    if (sensor == QStringLiteral("light"))
        return {QStringLiteral("light1"), QStringLiteral("light2")};
    if (sensor == QStringLiteral("mpu6050"))
        return {QStringLiteral("ax"), QStringLiteral("ay"), QStringLiteral("az"),
                QStringLiteral("gx"), QStringLiteral("gy"), QStringLiteral("gz")};
    return {};
}
}

MetricCard::MetricCard(const QString &title, const QString &unit,
                       const QColor &accent, QWidget *parent)
    : QWidget(parent), m_accent(accent) {
    setObjectName(QStringLiteral("metricCard"));
    setMinimumHeight(128);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 17, 20, 17);
    layout->setSpacing(4);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("metricTitle"));
    layout->addWidget(titleLabel);
    auto *valueRow = new QHBoxLayout;
    valueRow->setSpacing(6);
    m_valueLabel = new QLabel(QStringLiteral("--"));
    m_valueLabel->setObjectName(QStringLiteral("metricValue"));
    auto *unitLabel = new QLabel(unit);
    unitLabel->setObjectName(QStringLiteral("metricUnit"));
    valueRow->addWidget(m_valueLabel);
    valueRow->addWidget(unitLabel, 0, Qt::AlignBottom);
    valueRow->addStretch();
    layout->addLayout(valueRow);
    m_subtitleLabel = mutedLabel(QStringLiteral("等待设备数据"));
    layout->addWidget(m_subtitleLabel);
}

void MetricCard::setValue(double value, int precision) {
    m_valueLabel->setText(QString::number(value, 'f', precision));
}

void MetricCard::setTextValue(const QString &value) { m_valueLabel->setText(value); }

void MetricCard::setSubtitle(const QString &subtitle) { m_subtitleLabel->setText(subtitle); }

void MetricCard::paintEvent(QPaintEvent *event) {
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient glow(width() - 100, 0, width(), height());
    QColor transparent = m_accent; transparent.setAlpha(0);
    QColor visible = m_accent; visible.setAlpha(52);
    glow.setColorAt(0, transparent);
    glow.setColorAt(1, visible);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 16, 16);
    painter.fillPath(path, glow);
    painter.setPen(QPen(m_accent, 3));
    painter.drawLine(20, height() - 2, width() - 20, height() - 2);
}

GaugeWidget::GaugeWidget(const QColor &accent, double maximum,
                         const QString &unit, QWidget *parent)
    : QWidget(parent), m_accent(accent), m_maximum(maximum), m_unit(unit) {
    setMinimumSize(150, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GaugeWidget::setValue(double value, int precision) {
    m_value = qBound(0.0, value, m_maximum);
    m_precision = precision;
    update();
}

void GaugeWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal diameter = qMin(width(), height()) - 28.0;
    const QRectF ring((width() - diameter) / 2.0, (height() - diameter) / 2.0,
                      diameter, diameter);
    QPen basePen(QColor(66, 77, 122, 120), 13, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(basePen);
    painter.drawArc(ring, 225 * 16, -270 * 16);
    QPen valuePen(m_accent, 13, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(valuePen);
    const qreal ratio = m_maximum > 0 ? m_value / m_maximum : 0.0;
    painter.drawArc(ring, 225 * 16, int(-270 * 16 * ratio));
    painter.setPen(Qt::white);
    QFont valueFont = painter.font();
    valueFont.setPointSize(29);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.drawText(ring.adjusted(0, 25, 0, -15), Qt::AlignCenter,
                     QString::number(m_value, 'f', m_precision));
    painter.setPen(QColor("#8B98B7"));
    QFont unitFont = painter.font();
    unitFont.setPointSize(12);
    unitFont.setBold(false);
    painter.setFont(unitFont);
    painter.drawText(ring.adjusted(0, 70, 0, 0), Qt::AlignCenter, m_unit);
}

BarChartWidget::BarChartWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void BarChartWidget::append(double value) {
    m_values.append(qMax(0.0, value));
    while (m_values.size() > 18) m_values.removeFirst();
    update();
}

void BarChartWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = rect().adjusted(8, 12, -8, -12);
    painter.setPen(QPen(QColor(255, 255, 255, 18), 1, Qt::DashLine));
    for (int i = 1; i < 4; ++i) {
        const qreal y = plot.top() + plot.height() * i / 4.0;
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    if (m_values.isEmpty()) {
        painter.setPen(QColor("#8B98B7"));
        painter.setFont(QFont(painter.font().family(), 14));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("等待空气质量数据…"));
        return;
    }
    double maximum = 1.0;
    for (double value : m_values) maximum = qMax(maximum, value);
    const qreal slot = plot.width() / 18.0;
    const qreal barWidth = qMax(4.0, slot * 0.46);
    for (int i = 0; i < m_values.size(); ++i) {
        const qreal ratio = m_values.at(i) / maximum;
        const qreal height = plot.height() * ratio;
        const qreal x = plot.left() + (i + 0.5) * slot - barWidth / 2.0;
        QRectF bar(x, plot.bottom() - height, barWidth, height);
        QLinearGradient gradient(bar.topLeft(), bar.bottomLeft());
        gradient.setColorAt(0, QColor("#2CD9FF"));
        gradient.setColorAt(1, QColor("#0075FF"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(bar, barWidth / 2.0, barWidth / 2.0);
    }
}

TelemetryChart::TelemetryChart(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(265);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void TelemetryChart::append(double temperature, double humidity) {
    if (!qIsNaN(temperature)) m_temperature.append(temperature);
    else if (!m_temperature.isEmpty()) m_temperature.append(m_temperature.last());
    if (!qIsNaN(humidity)) m_humidity.append(humidity);
    else if (!m_humidity.isEmpty()) m_humidity.append(m_humidity.last());
    while (m_temperature.size() > kMaxChartPoints) m_temperature.removeFirst();
    while (m_humidity.size() > kMaxChartPoints) m_humidity.removeFirst();
    update();
}

void TelemetryChart::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = rect().adjusted(44, 14, -18, -34);
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int i = 0; i <= 4; ++i) {
        const qreal y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int i = 0; i <= 6; ++i) {
        const qreal x = plot.left() + plot.width() * i / 6.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    p.setPen(QColor(139, 152, 183));
    p.setFont(QFont(p.font().family(), 11));
    p.drawText(QRectF(0, plot.top() - 4, 38, 20), Qt::AlignRight, QStringLiteral("100"));
    p.drawText(QRectF(0, plot.center().y() - 10, 38, 20), Qt::AlignRight, QStringLiteral("50"));
    p.drawText(QRectF(0, plot.bottom() - 14, 38, 20), Qt::AlignRight, QStringLiteral("0"));
    p.drawText(QRectF(plot.left(), plot.bottom() + 9, plot.width(), 18), Qt::AlignCenter,
               QStringLiteral("最近 %1 个采样点").arg(kMaxChartPoints));
    if (m_temperature.isEmpty() && m_humidity.isEmpty()) {
        p.setPen(QColor(139, 152, 183));
        p.setFont(QFont(p.font().family(), 14));
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("等待实时数据流…"));
        return;
    }
    auto drawSeries = [&](const QVector<double> &values, QColor color, double min, double max) {
        if (values.isEmpty()) return;
        QPainterPath line;
        for (int i = 0; i < values.size(); ++i) {
            const qreal x = plot.left() + plot.width() * i / qMax(1, kMaxChartPoints - 1);
            const qreal normalized = qBound(0.0, (values[i] - min) / (max - min), 1.0);
            const qreal y = plot.bottom() - plot.height() * normalized;
            if (i == 0) line.moveTo(x, y); else line.lineTo(x, y);
        }
        QPainterPath area = line;
        area.lineTo(plot.left() + plot.width() * (values.size() - 1) / qMax(1, kMaxChartPoints - 1), plot.bottom());
        area.lineTo(plot.left(), plot.bottom());
        QLinearGradient fill(0, plot.top(), 0, plot.bottom());
        QColor top = color; top.setAlpha(70);
        QColor bottom = color; bottom.setAlpha(0);
        fill.setColorAt(0, top); fill.setColorAt(1, bottom);
        p.fillPath(area, fill);
        p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(line);
    };
    drawSeries(m_humidity, QColor("#6A5CFF"), 0.0, 100.0);
    drawSeries(m_temperature, QColor("#00E5FF"), 0.0, 50.0);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Env Vision · 环境监测中心"));
    resize(1480, 900);
    setMinimumSize(1120, 720);
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    auto *root = new QWidget;
    root->setObjectName(QStringLiteral("root"));
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(14);
    rootLayout->addWidget(createSidebar());
    m_pages = new QStackedWidget;
    m_pages->addWidget(createOverviewPage());
    m_pages->addWidget(createDataPage());
    m_pages->addWidget(createDevicePage());
    m_pages->addWidget(createSettingsPage());
    rootLayout->addWidget(m_pages, 1);
    setCentralWidget(root);
    applyTheme();
    if (!m_navButtons.isEmpty()) setPage(0, m_navButtons.first());
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::updateClock);
    m_clockTimer->start(1000);
    updateClock();
    QSettings settings;
    m_portEdit->setText(settings.value(QStringLiteral("network/port"), 8080).toString());
    QTimer::singleShot(0, this, &MainWindow::startServer);
}

MainWindow::~MainWindow() { stopServer(); }

QWidget *MainWindow::createSidebar() {
    auto *side = new QFrame;
    side->setObjectName(QStringLiteral("sidebar"));
    side->setFixedWidth(240);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(18, 22, 18, 18);
    layout->setSpacing(9);
    auto *brand = new QLabel(QStringLiteral("◈  环境监测"));
    brand->setObjectName(QStringLiteral("brand"));
    layout->addWidget(brand);
    auto *tagline = mutedLabel(QStringLiteral("REAL-TIME MONITOR"));
    tagline->setObjectName(QStringLiteral("brandTag"));
    layout->addWidget(tagline);
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setObjectName(QStringLiteral("divider"));
    layout->addWidget(line);
    layout->addSpacing(6);
    layout->addWidget(createNavButton(QStringLiteral("环境监测"), QStringLiteral("⌂"), 0));
    layout->addWidget(createNavButton(QStringLiteral("设置"), QStringLiteral("⚙"), 3));
    layout->addWidget(createNavButton(QStringLiteral("历史查询"), QStringLiteral("⌁"), 1));
    layout->addWidget(createNavButton(QStringLiteral("设备事件"), QStringLiteral("◇"), 2));
    layout->addStretch();
    auto *directCard = new QFrame;
    directCard->setObjectName(QStringLiteral("directCard"));
    auto *directLayout = new QVBoxLayout(directCard);
    directLayout->setContentsMargins(14, 14, 14, 14);
    auto *directTitle = new QLabel(QStringLiteral("DIRECT LINK"));
    directTitle->setObjectName(QStringLiteral("smallCaps"));
    directLayout->addWidget(directTitle);
    auto *directText = new QLabel(QStringLiteral("ESP32 直连模式\n数据不经过云端"));
    directText->setWordWrap(true);
    directText->setObjectName(QStringLiteral("smallText"));
    directLayout->addWidget(directText);
    layout->addWidget(directCard);
    return side;
}

QPushButton *MainWindow::createNavButton(const QString &text, const QString &glyph, int page) {
    auto *button = new QPushButton(QStringLiteral("%1   %2").arg(glyph, text));
    button->setObjectName(QStringLiteral("navButton"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, [this, page, button] { setPage(page, button); });
    m_navButtons.append(button);
    return button;
}

void MainWindow::setPage(int index, QPushButton *source) {
    if (m_pages) m_pages->setCurrentIndex(index);
    for (auto *button : m_navButtons) button->setChecked(button == source);
}

QWidget *MainWindow::createAirQualityPanel(QWidget *parent) {
    auto *content = new QWidget(parent);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // 等级色带 + 当前值定位标尺
    m_airScale = new QWidget;
    m_airScale->setObjectName(QStringLiteral("airScale"));
    m_airScale->setFixedHeight(30);
    m_airScale->setMinimumWidth(180);
    m_airScale->setToolTip(QStringLiteral("当前值按对数刻度定位：0 - 65 - 220 - 660 - 2200 - 20000 ppb"));
    m_airScaleMarker = new QWidget(m_airScale);
    m_airScaleMarker->setObjectName(QStringLiteral("airScaleMarker"));
    m_airScaleMarker->setFixedSize(4, 22);
    m_airScaleMarker->move(2, 4);
    m_airScaleMarker->hide();
    layout->addWidget(m_airScale);

    // 当前读数 + 等级徽标
    auto *reading = new QHBoxLayout;
    reading->setSpacing(8);
    reading->addWidget(mutedLabel(QStringLiteral("当前 TVOC")));
    m_airValue = new QLabel(QStringLiteral("-- ppb"));
    m_airValue->setObjectName(QStringLiteral("airValue"));
    reading->addWidget(m_airValue);
    m_airLevelBadge = new QLabel(QStringLiteral("等待数据"));
    m_airLevelBadge->setObjectName(QStringLiteral("airLevelBadge"));
    m_airLevelBadge->setProperty("level", QStringLiteral("idle"));
    reading->addWidget(m_airLevelBadge);
    reading->addStretch();
    reading->addWidget(mutedLabel(QStringLiteral("AGS02MA · 实时")));
    layout->addLayout(reading);

    // 历史趋势柱状图
    m_airChart = new BarChartWidget;
    m_airChart->setMinimumHeight(96);
    layout->addWidget(m_airChart, 1);

    // 当前空气质量情况
    auto *situationTitle = new QLabel(QStringLiteral("当前空气质量情况"));
    situationTitle->setObjectName(QStringLiteral("airSectionTitle"));
    layout->addWidget(situationTitle);
    m_airLevel = new QLabel(QStringLiteral("等待空气质量数据…"));
    m_airLevel->setObjectName(QStringLiteral("airLevelText"));
    m_airLevel->setProperty("level", QStringLiteral("idle"));
    m_airLevel->setWordWrap(true);
    m_airAdvice = new QLabel(QStringLiteral("接收 AGS02MA 数据后自动判定 TVOC 等级与处置建议。"));
    m_airAdvice->setObjectName(QStringLiteral("airAdvice"));
    m_airAdvice->setWordWrap(true);
    layout->addWidget(m_airLevel);
    layout->addWidget(m_airAdvice);

    // 等级标准说明（可折叠，默认收起）
    auto *details = new QFrame;
    details->setObjectName(QStringLiteral("airDetails"));
    auto *detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(12, 10, 12, 12);
    detailsLayout->setSpacing(6);

    static const char *kLegend[] = {
        "#01D7A7", "非常好", "0 – 65 ppb", "目标值，无任何关切。",
        "#7CD93A", "好", "65 – 220 ppb", "无相关关切，建议通风。",
        "#FFC13B", "中等", "220 – 660 ppb", "需要关注，建议加强通风。",
        "#FF8A3D", "差", "660 – 2200 ppb", "令人担忧，必须通风并查找污染源。",
        "#FF4D5E", "不健康", "2200 ppb 以上", "情况不可接受，仅在不可避免时使用。",
    };
    for (int i = 0; i < 5; ++i) {
        const int base = i * 4;
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto *dot = new QLabel(QStringLiteral("●"));
        dot->setObjectName(QStringLiteral("airLegendDot"));
        dot->setStyleSheet(QStringLiteral("color:%1;").arg(QLatin1String(kLegend[base])));
        dot->setFixedWidth(12);
        rowLayout->addWidget(dot, 0, Qt::AlignTop);

        auto *name = new QLabel(QString::fromUtf8(kLegend[base + 1]));
        name->setObjectName(QStringLiteral("airLegendName"));
        name->setFixedWidth(46);
        rowLayout->addWidget(name, 0, Qt::AlignTop);

        auto *range = new QLabel(QString::fromUtf8(kLegend[base + 2]));
        range->setObjectName(QStringLiteral("airLegendRange"));
        range->setFixedWidth(112);
        rowLayout->addWidget(range, 0, Qt::AlignTop);

        auto *note = new QLabel(QString::fromUtf8(kLegend[base + 3]));
        note->setObjectName(QStringLiteral("airLegendNote"));
        note->setWordWrap(true);
        rowLayout->addWidget(note, 1);

        detailsLayout->addWidget(row);
    }
    details->hide();

    auto *toggle = new QPushButton(QStringLiteral("▾  查看等级标准说明"));
    toggle->setObjectName(QStringLiteral("airToggle"));
    toggle->setCheckable(true);
    toggle->setCursor(Qt::PointingHandCursor);
    connect(toggle, &QPushButton::toggled, this, [details, toggle](bool expanded) {
        details->setVisible(expanded);
        toggle->setText(expanded ? QStringLiteral("▴  收起等级标准说明")
                                 : QStringLiteral("▾  查看等级标准说明"));
    });
    layout->addWidget(toggle, 0, Qt::AlignLeft);
    layout->addWidget(details);
    layout->addStretch();
    return content;
}

QWidget *MainWindow::createOverviewPage() {
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(10, 4, 4, 6);
    pageLayout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    auto *eyebrow = new QLabel(QStringLiteral("主页  /  环境监测"));
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    auto *title = new QLabel(QStringLiteral("环境监测"));
    title->setObjectName(QStringLiteral("pageTitle"));
    titles->addWidget(eyebrow);
    titles->addWidget(title);
    header->addLayout(titles);
    header->addStretch();
    auto *clockBox = new QVBoxLayout;
    m_clockLabel = new QLabel;
    m_clockLabel->setObjectName(QStringLiteral("clock"));
    m_clockLabel->setAlignment(Qt::AlignRight);
    auto *dateLabel = mutedLabel(QStringLiteral("本地时间 · Asia/Shanghai"));
    dateLabel->setAlignment(Qt::AlignRight);
    clockBox->addWidget(m_clockLabel);
    clockBox->addWidget(dateLabel);
    header->addLayout(clockBox);
    pageLayout->addLayout(header);

    auto *connection = new QFrame;
    connection->setObjectName(QStringLiteral("connectionBar"));
    auto *connectionLayout = new QHBoxLayout(connection);
    connectionLayout->setContentsMargins(18, 12, 14, 12);
    m_connectionPill = new QLabel(QStringLiteral("  ●  等待设备  "));
    m_connectionPill->setObjectName(QStringLiteral("statusPill"));
    connectionLayout->addWidget(m_connectionPill);
    m_connectionDetail = mutedLabel(QStringLiteral("WebSocket 服务正在准备…"));
    connectionLayout->addWidget(m_connectionDetail);
    connectionLayout->addStretch();
    connectionLayout->addWidget(mutedLabel(QStringLiteral("最后帧")));
    m_lastFrameLabel = mutedLabel(QStringLiteral("--:--:--"));
    connectionLayout->addWidget(m_lastFrameLabel);
    m_frameCountLabel = mutedLabel(QStringLiteral("0 帧"));
    connectionLayout->addWidget(m_frameCountLabel);
    m_rateLabel = mutedLabel(QStringLiteral("0.0 frame/s"));
    connectionLayout->addWidget(m_rateLabel);
    auto *protocol = new QLabel(QStringLiteral("COMPACT FRAME"));
    protocol->setObjectName(QStringLiteral("protocolTag"));
    connectionLayout->addWidget(protocol);
    pageLayout->addWidget(connection);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("overviewScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 2, 6, 8);
    layout->setSpacing(14);

    auto *summary = new QHBoxLayout;
    summary->setSpacing(14);
    m_motionSummaryCard = new MetricCard(QStringLiteral("上次检测到人体"), QString(), QColor("#01D7A7"));
    m_motionSummaryCard->setTextValue(QStringLiteral("--:--"));
    m_motionSummaryCard->setSubtitle(QStringLiteral("PIR 等待采样"));
    m_uptimeCard = new MetricCard(QStringLiteral("运行时间"), QString(), QColor("#6A5CFF"));
    m_uptimeCard->setTextValue(QStringLiteral("00:00:00"));
    m_uptimeCard->setSubtitle(QStringLiteral("上位机本次会话"));
    m_rateSummaryCard = new MetricCard(QStringLiteral("数据接收速率"), QStringLiteral("帧/秒"), QColor("#00E5FF"));
    m_rateSummaryCard->setValue(0.0);
    m_rateSummaryCard->setSubtitle(QStringLiteral("WebSocket 实时统计"));
    m_connectionSummaryCard = new MetricCard(QStringLiteral("与下位机连接"), QString(), QColor("#FFB547"));
    m_connectionSummaryCard->setTextValue(QStringLiteral("等待中"));
    m_connectionSummaryCard->setSubtitle(QStringLiteral("监听端口 8080"));
    summary->addWidget(m_motionSummaryCard);
    summary->addWidget(m_uptimeCard);
    summary->addWidget(m_rateSummaryCard);
    summary->addWidget(m_connectionSummaryCard);
    layout->addLayout(summary);

    auto *sensorRow = new QHBoxLayout;
    sensorRow->setSpacing(14);
    auto *motionContent = new QWidget;
    auto *motionLayout = new QVBoxLayout(motionContent);
    motionLayout->setContentsMargins(0, 0, 0, 0);
    m_motionLabel = new QLabel(QStringLiteral("○  空间状态等待检测"));
    m_motionLabel->setObjectName(QStringLiteral("motionHero"));
    m_motionDetail = mutedLabel(QStringLiteral("PIR · 暂无数据"));
    m_accelLabel = new QLabel(QStringLiteral("ACC\n--  /  --  /  --"));
    m_accelLabel->setObjectName(QStringLiteral("vectorValue"));
    m_gyroLabel = mutedLabel(QStringLiteral("GYRO   --  /  --  /  --"));
    motionLayout->addWidget(m_motionLabel);
    motionLayout->addWidget(m_motionDetail);
    motionLayout->addStretch();
    motionLayout->addWidget(m_accelLabel);
    motionLayout->addWidget(m_gyroLabel);
    auto *motionPanel = createPanel(QStringLiteral("加速度与空间活动"), motionContent);
    motionPanel->setMinimumHeight(300);
    sensorRow->addWidget(motionPanel, 5);

    auto *soundContent = new QWidget;
    auto *soundLayout = new QVBoxLayout(soundContent);
    soundLayout->setContentsMargins(0, 0, 0, 0);
    soundLayout->addWidget(mutedLabel(QStringLiteral("VOLUME DETECTION")), 0, Qt::AlignHCenter);
    m_soundGauge = new GaugeWidget(QColor("#00DDF9"), 100.0, QStringLiteral("dB"));
    soundLayout->addWidget(m_soundGauge, 1);
    auto *soundPanel = createPanel(QStringLiteral("音量监测"), soundContent);
    soundPanel->setMinimumHeight(300);
    sensorRow->addWidget(soundPanel, 3);

    auto *lightContent = new QWidget;
    auto *lightLayout = new QHBoxLayout(lightContent);
    lightLayout->setContentsMargins(0, 0, 0, 0);
    auto *lightText = new QVBoxLayout;
    lightText->addWidget(mutedLabel(QStringLiteral("双通道 ADC")));
    m_lightChannels = new QLabel(QStringLiteral("L1  --\nL2  --"));
    m_lightChannels->setObjectName(QStringLiteral("vectorValueSmall"));
    lightText->addWidget(m_lightChannels);
    lightText->addStretch();
    lightLayout->addLayout(lightText, 1);
    m_lightGauge = new GaugeWidget(QColor("#01D7A7"), 4095.0, QStringLiteral("ADC"));
    lightLayout->addWidget(m_lightGauge, 2);
    auto *lightPanel = createPanel(QStringLiteral("光强监测"), lightContent);
    lightPanel->setMinimumHeight(300);
    sensorRow->addWidget(lightPanel, 4);
    layout->addLayout(sensorRow);

    auto *chartRow = new QHBoxLayout;
    chartRow->setSpacing(14);
    auto *chartContent = new QWidget;
    auto *chartLayout = new QVBoxLayout(chartContent);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    auto *legend = new QHBoxLayout;
    auto *tempLegend = mutedLabel(QStringLiteral("● 温度"));
    tempLegend->setStyleSheet(QStringLiteral("color:#00E5FF"));
    m_temperatureValue = new QLabel(QStringLiteral("-- °C"));
    m_temperatureValue->setObjectName(QStringLiteral("chartValue"));
    auto *humidLegend = mutedLabel(QStringLiteral("● 湿度"));
    humidLegend->setStyleSheet(QStringLiteral("color:#8075FF"));
    m_humidityValue = new QLabel(QStringLiteral("-- %RH"));
    m_humidityValue->setObjectName(QStringLiteral("chartValue"));
    legend->addWidget(tempLegend);
    legend->addWidget(m_temperatureValue);
    legend->addWidget(humidLegend);
    legend->addWidget(m_humidityValue);
    legend->addStretch();
    auto *live = new QLabel(QStringLiteral("● LIVE"));
    live->setObjectName(QStringLiteral("liveTag"));
    legend->addWidget(live);
    chartLayout->addLayout(legend);
    m_chart = new TelemetryChart;
    chartLayout->addWidget(m_chart, 1);
    auto *temperaturePanel = createPanel(QStringLiteral("温湿度测量"), chartContent);
    temperaturePanel->setMinimumHeight(370);
    chartRow->addWidget(temperaturePanel, 7);

    auto *airPanel = createPanel(QStringLiteral("空气质量"), createAirQualityPanel());
    airPanel->setMinimumHeight(370);
    chartRow->addWidget(airPanel, 5);
    layout->addLayout(chartRow);

    auto *bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(14);
    m_overviewTable = new QTableWidget(0, 4);
    m_overviewTable->setObjectName(QStringLiteral("dataTable"));
    m_overviewTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("传感器"),
                                                QStringLiteral("字段"), QStringLiteral("数值")});
    m_overviewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_overviewTable->verticalHeader()->hide();
    m_overviewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_overviewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_overviewTable->setAlternatingRowColors(true);
    m_overviewTable->setMinimumHeight(285);
    bottomRow->addWidget(createPanel(QStringLiteral("最近传感器数据"), m_overviewTable), 8);
    m_overviewEvents = new QListWidget;
    m_overviewEvents->setObjectName(QStringLiteral("timelineList"));
    m_overviewEvents->setSpacing(5);
    m_overviewEvents->addItem(QStringLiteral("●  上位机界面已启动"));
    m_overviewEvents->setMinimumHeight(285);
    bottomRow->addWidget(createPanel(QStringLiteral("设备事件时间线"), m_overviewEvents), 4);
    layout->addLayout(bottomRow);

    scroll->setWidget(content);
    pageLayout->addWidget(scroll, 1);
    return page;
}

QWidget *MainWindow::createDataPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 4, 10, 10);
    layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("实时数据流"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(mutedLabel(QStringLiteral("最近 500 条传感器字段 · 新数据置顶")));
    m_dataTable = new QTableWidget(0, 4);
    m_dataTable->setObjectName(QStringLiteral("dataTable"));
    m_dataTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("传感器"), QStringLiteral("字段"), QStringLiteral("数值")});
    m_dataTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_dataTable->verticalHeader()->hide();
    m_dataTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dataTable->setAlternatingRowColors(true);
    layout->addWidget(m_dataTable, 1);
    return page;
}

QWidget *MainWindow::createDevicePage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 4, 10, 10);
    layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("设备与事件"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(mutedLabel(QStringLiteral("WebSocket 连接、协议解析与异常事件")));
    m_logList = new QListWidget;
    m_logList->setObjectName(QStringLiteral("logList"));
    m_logList->setSpacing(3);
    layout->addWidget(m_logList, 1);
    return page;
}

QWidget *MainWindow::createSettingsPage() {
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 4, 10, 10);
    layout->setSpacing(14);
    auto *title = new QLabel(QStringLiteral("连接设置"));
    title->setObjectName(QStringLiteral("pageTitle"));
    layout->addWidget(title);
    layout->addWidget(mutedLabel(QStringLiteral("上位机作为 WebSocket 服务端，ESP32 主动连接到本机地址")));
    auto *formWidget = new QWidget;
    auto *form = new QFormLayout(formWidget);
    form->setContentsMargins(0, 8, 0, 8);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(16);
    m_portEdit = new QLineEdit(QStringLiteral("8080"));
    m_portEdit->setPlaceholderText(QStringLiteral("1 - 65535"));
    m_portEdit->setMaximumWidth(240);
    form->addRow(QStringLiteral("监听端口"), m_portEdit);
    auto *address = new QLabel(localAddressSummary());
    address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("固件连接地址"), address);
    layout->addWidget(createPanel(QStringLiteral("WebSocket 直连"), formWidget));
    m_serverButton = new QPushButton(QStringLiteral("停止监听"));
    m_serverButton->setObjectName(QStringLiteral("primaryButton"));
    m_serverButton->setCursor(Qt::PointingHandCursor);
    connect(m_serverButton, &QPushButton::clicked, this, [this] {
        if (m_server && m_server->isListening()) stopServer(); else startServer();
    });
    layout->addWidget(m_serverButton, 0, Qt::AlignLeft);
    auto *hint = new QLabel(QStringLiteral(
        "固件侧将 SERVER_URL 设置为 ws://本机局域网IP:端口/。\n"
        "当前版本仅在内存中保留图表与表格数据；未来接入服务器时可复用解析层，替换数据源即可。"));
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("hintBox"));
    layout->addWidget(hint);
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createPanel(const QString &title, QWidget *content) {
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("panel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    auto *label = new QLabel(title);
    label->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(label);
    if (content) layout->addWidget(content, 1);
    return panel;
}

void MainWindow::applyTheme() {
    setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget#root { background: #06101F; color: #F4F7FF; }
        QFrame#sidebar { background: rgba(8, 20, 39, 245); border: 1px solid #172B4D; border-radius: 18px; }
        QLabel#brand { color: #FFFFFF; font-size: 22px; font-weight: 800; letter-spacing: 2px; padding: 3px; }
        QLabel#brandTag, QLabel#smallCaps, QLabel#eyebrow { color: #00DDF9; font-size: 13px; font-weight: 700; letter-spacing: 2px; }
        QFrame#divider { color: #1A2D4B; background: #1A2D4B; max-height: 1px; border: none; }
        QPushButton#navButton { color: #8B98B7; background: transparent; border: none; border-radius: 11px; padding: 12px 14px; text-align: left; font-size: 17px; font-weight: 600; }
        QPushButton#navButton:hover { color: white; background: rgba(0, 229, 255, 20); }
        QPushButton#navButton:checked { color: white; background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #087EA4, stop:1 #153B78); border-left: 3px solid #00E5FF; }
        QFrame#directCard { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #0B4B69, stop:1 #261C63); border: 1px solid #285A7D; border-radius: 14px; }
        QLabel#smallText { color: #B8C4DB; font-size: 15px; }
        QLabel#pageTitle { font-size: 32px; font-weight: 800; color: #FFFFFF; }
        QLabel#clock { font-size: 23px; font-weight: 700; color: #FFFFFF; }
        QLabel#muted { color: #8B98B7; font-size: 15px; }
        QFrame#connectionBar, QFrame#motionBar { background: #0B1B32; border: 1px solid #173255; border-radius: 13px; }
        QLabel#statusPill { color: #FFD166; background: #2A2531; border: 1px solid #6B5738; border-radius: 10px; min-height: 30px; font-size: 15px; font-weight: 700; }
        QLabel#protocolTag, QLabel#liveTag { color: #00E5FF; background: #0A3045; border: 1px solid #14596B; border-radius: 8px; padding: 5px 9px; font-size: 13px; font-weight: 700; }
        QWidget#metricCard, QFrame#panel { background: #0A1930; border: 1px solid #163052; border-radius: 16px; }
        QWidget#metricCard:hover, QFrame#panel:hover { border: 1px solid #24517D; }
        QLabel#metricTitle { color: #96A3BF; font-size: 15px; font-weight: 600; }
        QLabel#metricValue { color: white; font-size: 33px; font-weight: 800; }
        QLabel#metricUnit { color: #8B98B7; font-size: 14px; padding-bottom: 5px; }
        QLabel#panelTitle { color: white; font-size: 19px; font-weight: 700; }
        QLabel#statValue { color: #00E5FF; font-size: 30px; font-weight: 800; }
        QLabel#statValueSmall { color: #FFFFFF; font-size: 22px; font-weight: 700; }
        QLabel#motionTitle { color: #C5D0E5; font-size: 16px; font-weight: 700; }
        QLabel#motionHero { color: #EAF1FF; font-size: 23px; font-weight: 800; }
        QLabel#vectorValue { color: #FFFFFF; font-size: 30px; font-weight: 800; line-height: 1.4; }
        QLabel#vectorValueSmall { color: #FFFFFF; font-size: 23px; font-weight: 700; }
        QLabel#chartValue { color: #FFFFFF; font-size: 17px; font-weight: 800; margin-right: 14px; }
        QLabel#airValue { color: #2CD9FF; font-size: 28px; font-weight: 800; }
        QLabel#airLevelBadge { color: #7E8DB0; background: #0E2039; border: 1px solid #1D3A60; border-radius: 9px; padding: 4px 10px; font-size: 15px; font-weight: 800; }
        QLabel#airLevelBadge[level="excellent"] { color: #01D7A7; background: #0B2E2B; border-color: #14705F; }
        QLabel#airLevelBadge[level="good"] { color: #7CD93A; background: #1B2E15; border-color: #4E7A2B; }
        QLabel#airLevelBadge[level="moderate"] { color: #FFC13B; background: #33280F; border-color: #7A6021; }
        QLabel#airLevelBadge[level="poor"] { color: #FF8A3D; background: #38230F; border-color: #8A4E21; }
        QLabel#airLevelBadge[level="unhealthy"] { color: #FF6B7A; background: #3A1620; border-color: #8F2C3D; }
        QWidget#airScale { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #01D7A7, stop:0.2 #7CD93A, stop:0.4 #FFC13B, stop:0.6 #FF8A3D, stop:0.8 #FF4D5E, stop:1 #A3122B); border: 1px solid #173255; border-radius: 8px; }
        QWidget#airScaleMarker { background: #FFFFFF; border: 1px solid #06101F; border-radius: 2px; }
        QLabel#airSectionTitle { color: #96A3BF; font-size: 14px; font-weight: 700; letter-spacing: 2px; }
        QLabel#airLevelText { font-size: 20px; font-weight: 800; color: #EAF1FF; }
        QLabel#airLevelText[level="excellent"] { color: #01D7A7; }
        QLabel#airLevelText[level="good"] { color: #7CD93A; }
        QLabel#airLevelText[level="moderate"] { color: #FFC13B; }
        QLabel#airLevelText[level="poor"] { color: #FF8A3D; }
        QLabel#airLevelText[level="unhealthy"] { color: #FF6B7A; }
        QLabel#airAdvice { color: #B8C4DB; font-size: 15px; }
        QPushButton#airToggle { color: #00DDF9; background: transparent; border: none; padding: 4px 0; font-size: 15px; font-weight: 700; text-align: left; }
        QPushButton#airToggle:hover { color: #7DF2FF; }
        QFrame#airDetails { background: #08172C; border: 1px solid #173255; border-radius: 10px; }
        QLabel#airLegendName { color: #EAF1FF; font-size: 15px; font-weight: 700; }
        QLabel#airLegendRange { color: #8B98B7; font-size: 14px; }
        QLabel#airLegendNote { color: #B8C4DB; font-size: 15px; }
        QScrollArea#overviewScroll, QScrollArea#overviewScroll > QWidget > QWidget { background: transparent; border: none; }
        QLineEdit { color: white; background: #08172C; border: 1px solid #214469; border-radius: 9px; padding: 10px 12px; font-size: 16px; selection-background-color: #087EA4; }
        QLineEdit:focus { border-color: #00DDF9; }
        QPushButton#primaryButton { color: #03111F; background: #00DDF9; border: none; border-radius: 9px; padding: 11px 22px; font-size: 16px; font-weight: 800; }
        QPushButton#primaryButton:hover { background: #57ECFF; }
        QLabel#hintBox { color: #9AA8C2; background: #0F2544; border: 1px solid #1D3A60; border-radius: 12px; padding: 16px; font-size: 15px; }
        QTableWidget#dataTable, QListWidget#logList { color: #DDE7F8; background: #081427; alternate-background-color: #0D203B; border: 1px solid #173255; border-radius: 13px; gridline-color: #173255; selection-background-color: #124C70; outline: none; font-size: 15px; }
        QTableWidget#dataTable::item { padding: 7px; }
        QHeaderView::section { color: #8FA0BD; background: #0D203B; border: none; border-bottom: 1px solid #244261; padding: 10px; font-size: 15px; font-weight: 700; }
        QListWidget#logList::item { padding: 9px 12px; border-bottom: 1px solid #142B48; }
        QListWidget#logList::item:selected { background: #123A5B; }
        QListWidget#timelineList { color: #C9D4E8; background: transparent; border: none; outline: none; font-size: 15px; }
        QListWidget#timelineList::item { padding: 9px 4px 9px 12px; border-left: 2px solid #0075FF; }
        QListWidget#timelineList::item:selected { background: #102A48; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
        QScrollBar::handle:vertical { background: #284766; border-radius: 4px; min-height: 24px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    )"));
}

void MainWindow::startServer() {
    bool ok = false;
    const int port = m_portEdit->text().toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        appendLog(QStringLiteral("监听端口无效：%1").arg(m_portEdit->text()), QStringLiteral("ERROR"));
        m_connectionDetail->setText(QStringLiteral("请输入 1 - 65535 范围内的端口"));
        return;
    }
    stopServer();
    m_server = new QWebSocketServer(QStringLiteral("Env Vision Monitor"), QWebSocketServer::NonSecureMode, this);
    connect(m_server, &QWebSocketServer::newConnection, this, [this] {
        while (m_server->hasPendingConnections()) {
            QWebSocket *client = m_server->nextPendingConnection();
            m_clients.append(client);
            appendLog(QStringLiteral("设备已连接：%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort()), QStringLiteral("LINK"));
            connect(client, &QWebSocket::textMessageReceived, this,
                    [this, client](const QString &message) { handleMessage(client, message); });
            connect(client, &QWebSocket::disconnected, this, [this, client] {
                appendLog(QStringLiteral("设备已断开：%1").arg(client->peerAddress().toString()), QStringLiteral("WARN"));
                m_clients.removeAll(client);
                client->deleteLater();
                updateConnectionState();
            });
        }
        updateConnectionState();
    });
    if (!m_server->listen(QHostAddress::AnyIPv4, quint16(port))) {
        appendLog(QStringLiteral("无法监听 %1：%2").arg(port).arg(m_server->errorString()), QStringLiteral("ERROR"));
        m_connectionDetail->setText(QStringLiteral("启动失败：%1").arg(m_server->errorString()));
        m_server->deleteLater();
        m_server = nullptr;
        updateConnectionState();
        return;
    }
    QSettings().setValue(QStringLiteral("network/port"), port);
    appendLog(QStringLiteral("WebSocket 服务已监听 0.0.0.0:%1").arg(port), QStringLiteral("READY"));
    updateConnectionState();
}

void MainWindow::stopServer() {
    for (QWebSocket *client : std::as_const(m_clients)) {
        client->disconnect(this);
        client->close(QWebSocketProtocol::CloseCodeGoingAway, QStringLiteral("Monitor stopped"));
        client->deleteLater();
    }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    updateConnectionState();
}

void MainWindow::updateConnectionState() {
    if (!m_connectionPill || !m_connectionDetail) return;
    if (!m_clients.isEmpty()) {
        m_connectionPill->setText(QStringLiteral("  ●  已连接  "));
        m_connectionPill->setStyleSheet(QStringLiteral("color:#01D7A7;background:#0C3836;border:1px solid #17685B;border-radius:10px;min-height:30px;font-size:15px;font-weight:700;"));
        m_connectionDetail->setText(QStringLiteral("%1 台设备正在通过 WebSocket 直连").arg(m_clients.size()));
        if (m_connectionSummaryCard) {
            m_connectionSummaryCard->setTextValue(QStringLiteral("已连接"));
            m_connectionSummaryCard->setSubtitle(QStringLiteral("%1 台设备在线").arg(m_clients.size()));
        }
    } else if (m_server && m_server->isListening()) {
        m_connectionPill->setText(QStringLiteral("  ●  等待设备  "));
        m_connectionPill->setStyleSheet(QString());
        m_connectionDetail->setText(QStringLiteral("正在监听 0.0.0.0:%1 · %2").arg(m_server->serverPort()).arg(localAddressSummary()));
        if (m_connectionSummaryCard) {
            m_connectionSummaryCard->setTextValue(QStringLiteral("等待中"));
            m_connectionSummaryCard->setSubtitle(QStringLiteral("监听端口 %1").arg(m_server->serverPort()));
        }
    } else {
        m_connectionPill->setText(QStringLiteral("  ●  已停止  "));
        m_connectionPill->setStyleSheet(QStringLiteral("color:#FF6B89;background:#371C30;border:1px solid #69314C;border-radius:10px;min-height:30px;font-size:15px;font-weight:700;"));
        m_connectionDetail->setText(QStringLiteral("WebSocket 服务未运行"));
        if (m_connectionSummaryCard) {
            m_connectionSummaryCard->setTextValue(QStringLiteral("已停止"));
            m_connectionSummaryCard->setSubtitle(QStringLiteral("请在设置中开始监听"));
        }
    }
    if (m_serverButton)
        m_serverButton->setText(m_server && m_server->isListening() ? QStringLiteral("停止监听") : QStringLiteral("开始监听"));
}

void MainWindow::handleMessage(QWebSocket *client, const QString &message) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        appendLog(QStringLiteral("%1 发来无法解析的数据：%2").arg(client->peerAddress().toString(), error.errorString()), QStringLiteral("ERROR"));
        return;
    }
    const QJsonObject root = document.object();
    const QJsonObject sensors = root.value(QStringLiteral("sensors")).toObject();
    if (sensors.isEmpty()) {
        appendLog(QStringLiteral("收到 JSON，但缺少 sensors 对象"), QStringLiteral("WARN"));
        return;
    }
    for (auto it = sensors.constBegin(); it != sensors.constEnd(); ++it)
        if (it.value().isObject()) parseSensorBlock(it.key(), it.value().toObject());
    ++m_frameCount;
    ++m_rateWindowFrames;
    m_lastFrameAt = QDateTime::currentMSecsSinceEpoch();
    m_lastFrameLabel->setText(displayTime(m_lastFrameAt));
    m_frameCountLabel->setText(QStringLiteral("%1 帧").arg(m_frameCount));
    const QJsonValue schema = root.value(QStringLiteral("schema_version"));
    const QString protocol = schema.isUndefined()
        ? QStringLiteral("compact")
        : QStringLiteral("schema v%1").arg(schema.toInt());
    appendLog(QStringLiteral("收到数据帧 #%1 · %2 · %3 个传感器")
                  .arg(root.value(QStringLiteral("frame_seq")).toVariant().toString(), protocol)
                  .arg(sensors.size()), QStringLiteral("DATA"));
}

void MainWindow::parseSensorBlock(const QString &name, const QJsonObject &block) {
    const qint64 first = qint64(block.value(QStringLiteral("first_sample_time_ms")).toDouble());
    const qint64 period = qint64(block.value(QStringLiteral("sample_period_ms")).toDouble());
    QStringList fields;
    const QJsonArray transmittedFields = block.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &field : transmittedFields)
        fields.append(field.toString());
    if (fields.isEmpty())
        fields = defaultFieldsForSensor(name);
    const QJsonArray samples = block.value(QStringLiteral("samples")).toArray();
    for (qsizetype i = 0; i < samples.size(); ++i) {
        const qint64 timestamp = first > 0 ? first + i * period : QDateTime::currentMSecsSinceEpoch();
        const QJsonValue sample = samples.at(i);
        if (sample.isArray() && !fields.isEmpty()) {
            const QJsonArray row = sample.toArray();
            for (qsizetype column = 0; column < fields.size() && column < row.size(); ++column)
                updateMetric(name, fields.at(column), jsonNumber(row.at(column)), timestamp);
        } else if (sample.isArray()) {
            const QJsonArray row = sample.toArray();
            for (qsizetype column = 0; column < row.size(); ++column)
                updateMetric(name, QStringLiteral("value_%1").arg(column),
                             jsonNumber(row.at(column)), timestamp);
        } else {
            updateMetric(name, QStringLiteral("value"), jsonNumber(sample), timestamp);
        }
    }
}

void MainWindow::updateMetric(const QString &sensor, const QString &field, double value, qint64 timestamp) {
    if (sensor == QStringLiteral("dht") && field == QStringLiteral("temperature")) {
        m_latestTemperature = value;
        if (m_temperatureValue)
            m_temperatureValue->setText(QStringLiteral("%1 °C").arg(value, 0, 'f', 1));
    } else if (sensor == QStringLiteral("dht") && field == QStringLiteral("humidity")) {
        m_latestHumidity = value;
        if (m_humidityValue)
            m_humidityValue->setText(QStringLiteral("%1 %RH").arg(value, 0, 'f', 1));
        m_chart->append(m_latestTemperature, m_latestHumidity);
    } else if (sensor == QStringLiteral("light") && (field == QStringLiteral("light1") || field == QStringLiteral("light2"))) {
        if (field == QStringLiteral("light1")) m_light1 = value;
        else m_light2 = value;
        if (m_lightChannels)
            m_lightChannels->setText(QStringLiteral("L1  %1\nL2  %2")
                                         .arg(qIsNaN(m_light1) ? QStringLiteral("--") : QString::number(m_light1, 'f', 0),
                                              qIsNaN(m_light2) ? QStringLiteral("--") : QString::number(m_light2, 'f', 0)));
        if (m_lightGauge) {
            const double average = qIsNaN(m_light1) ? m_light2
                                  : qIsNaN(m_light2) ? m_light1
                                  : (m_light1 + m_light2) / 2.0;
            if (!qIsNaN(average)) m_lightGauge->setValue(average, 0);
        }
    } else if (sensor == QStringLiteral("ags02")) {
        if (m_airValue) m_airValue->setText(QStringLiteral("%1 ppb").arg(value, 0, 'f', 0));
        updateAirQuality(value);
    } else if (sensor == QStringLiteral("sound")) {
        if (m_soundGauge) m_soundGauge->setValue(value, 1);
    } else if (sensor == QStringLiteral("pir")) {
        const bool active = value > 0.5;
        m_motionLabel->setText(active ? QStringLiteral("●  检测到人体活动") : QStringLiteral("○  空间处于静止状态"));
        m_motionLabel->setStyleSheet(active ? QStringLiteral("color:#01D7A7;font-weight:700;") : QString());
        m_motionDetail->setText(QStringLiteral("PIR · %1").arg(displayTime(timestamp)));
        if (active && m_motionSummaryCard) {
            m_motionSummaryCard->setTextValue(displayTime(timestamp).left(8));
            m_motionSummaryCard->setSubtitle(QStringLiteral("最近一次人体活动"));
            if (m_overviewEvents) {
                m_overviewEvents->insertItem(0, QStringLiteral("●  %1  检测到人体活动").arg(displayTime(timestamp).left(8)));
                while (m_overviewEvents->count() > 12)
                    delete m_overviewEvents->takeItem(m_overviewEvents->count() - 1);
            }
        }
    } else if (sensor == QStringLiteral("mpu6050")) {
        static double ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        if (field == "ax") ax = value; else if (field == "ay") ay = value; else if (field == "az") az = value;
        else if (field == "gx") gx = value; else if (field == "gy") gy = value; else if (field == "gz") gz = value;
        m_accelLabel->setText(QStringLiteral("ACC\n%1  /  %2  /  %3").arg(ax, 0, 'f', 1).arg(ay, 0, 'f', 1).arg(az, 0, 'f', 1));
        m_gyroLabel->setText(QStringLiteral("GYRO   %1  /  %2  /  %3").arg(gx, 0, 'f', 1).arg(gy, 0, 'f', 1).arg(gz, 0, 'f', 1));
    }
    addTableRow(timestamp, sensor, field, QString::number(value, 'f', qAbs(value) >= 100 ? 0 : 2));
}

void MainWindow::updateAirQuality(double ppb) {
    // TVOC 等级：0 - 65 / 65 - 220 / 220 - 660 / 660 - 2200 / 2200 ppb 以上
    static const QString kName[5] = {QStringLiteral("非常好"), QStringLiteral("好"),
                                     QStringLiteral("中等"), QStringLiteral("差"),
                                     QStringLiteral("不健康")};
    static const QString kAdvice[5] = {
        QStringLiteral("0 – 65 ppb · 目标值，无任何关切。"),
        QStringLiteral("65 – 220 ppb · 无相关关切，建议通风。"),
        QStringLiteral("220 – 660 ppb · 需要关注，建议加强通风。"),
        QStringLiteral("660 – 2200 ppb · 令人担忧，必须通风并查找污染源。"),
        QStringLiteral("2200 ppb 以上 · 情况不可接受，仅在不可避免时使用。"),
    };
    static const char *kLevelKey[5] = {"excellent", "good", "moderate", "poor", "unhealthy"};

    int index = 4;
    if (ppb < 65.0) index = 0;
    else if (ppb < 220.0) index = 1;
    else if (ppb < 660.0) index = 2;
    else if (ppb < 2200.0) index = 3;

    const QString levelKey = QLatin1String(kLevelKey[index]);
    const QString levelText = QStringLiteral("当前空气质量情况：%1").arg(kName[index]);
    if (m_airLevel) {
        m_airLevel->setText(levelText);
        m_airLevel->setProperty("level", levelKey);
        m_airLevel->style()->unpolish(m_airLevel);
        m_airLevel->style()->polish(m_airLevel);
    }
    if (m_airLevelBadge) {
        m_airLevelBadge->setText(kName[index]);
        m_airLevelBadge->setProperty("level", levelKey);
        m_airLevelBadge->style()->unpolish(m_airLevelBadge);
        m_airLevelBadge->style()->polish(m_airLevelBadge);
    }
    if (m_airAdvice) m_airAdvice->setText(kAdvice[index]);
    if (m_airChart) m_airChart->append(ppb);

    // 在色带上以对数刻度定位当前值：0 → 65 → 220 → 660 → 2200 → 20000 ppb
    if (m_airScale && m_airScaleMarker) {
        static const double kEdges[6] = {0.0, 65.0, 220.0, 660.0, 2200.0, 20000.0};
        const double clamped = qBound(kEdges[0], ppb, kEdges[5]);
        int segment = 0;
        while (segment < 4 && clamped > kEdges[segment + 1]) ++segment;
        const double span = qMax(1e-6, kEdges[segment + 1] - kEdges[segment]);
        const double ratio = qBound(0.0, (clamped - kEdges[segment]) / span, 1.0);
        const double position = (segment + ratio) / 5.0;
        const int travel = qMax(0, m_airScale->width() - m_airScaleMarker->width());
        m_airScaleMarker->move(qRound(position * travel), (m_airScale->height() - m_airScaleMarker->height()) / 2);
        m_airScaleMarker->show();
        m_airScaleMarker->raise();
    }

    if (m_overviewEvents && index >= 3) {
        const QString entry = QStringLiteral("●  %1  TVOC %2 ppb · %3")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                       QString::number(ppb, 'f', 0), kName[index]);
        if (m_overviewEvents->count() == 0 || m_overviewEvents->item(0)->text() != entry) {
            m_overviewEvents->insertItem(0, entry);
            while (m_overviewEvents->count() > 12)
                delete m_overviewEvents->takeItem(m_overviewEvents->count() - 1);
        }
    }
}

void MainWindow::addTableRow(qint64 timestamp, const QString &sensor, const QString &field, const QString &value) {
    auto addToTable = [&](QTableWidget *table, int limit) {
        if (!table) return;
        table->insertRow(0);
        table->setItem(0, 0, new QTableWidgetItem(displayTime(timestamp)));
        table->setItem(0, 1, new QTableWidgetItem(sensor));
        table->setItem(0, 2, new QTableWidgetItem(field));
        table->setItem(0, 3, new QTableWidgetItem(value));
        while (table->rowCount() > limit) table->removeRow(table->rowCount() - 1);
    };
    addToTable(m_dataTable, kMaxTableRows);
    addToTable(m_overviewTable, 8);
}

void MainWindow::appendLog(const QString &text, const QString &level) {
    const QString line = QStringLiteral("%1   [%2]   %3").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), level, text);
    if (m_logList) {
        m_logList->insertItem(0, line);
        while (m_logList->count() > kMaxLogRows) delete m_logList->takeItem(m_logList->count() - 1);
    }
    if (m_overviewEvents && level != QStringLiteral("DATA")) {
        m_overviewEvents->insertItem(0, QStringLiteral("●  %1").arg(line));
        while (m_overviewEvents->count() > 12)
            delete m_overviewEvents->takeItem(m_overviewEvents->count() - 1);
    }
}

QString MainWindow::localAddressSummary() const {
    QStringList addresses;
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            addresses.append(QStringLiteral("ws://%1:%2/").arg(address.toString()).arg(m_portEdit ? m_portEdit->text() : QStringLiteral("8080")));
    }
    return addresses.isEmpty() ? QStringLiteral("ws://127.0.0.1:8080/") : addresses.join(QStringLiteral("  ·  "));
}

void MainWindow::updateClock() {
    const QDateTime now = QDateTime::currentDateTime();
    if (m_clockLabel) m_clockLabel->setText(now.toString(QStringLiteral("HH:mm:ss")));
    if (m_uptimeCard && m_startedAt > 0) {
        const qint64 seconds = (now.toMSecsSinceEpoch() - m_startedAt) / 1000;
        m_uptimeCard->setTextValue(QStringLiteral("%1:%2:%3")
            .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    }
    if (m_rateWindowStart == 0) m_rateWindowStart = now.toMSecsSinceEpoch();
    const qint64 elapsed = now.toMSecsSinceEpoch() - m_rateWindowStart;
    if (elapsed >= 3000) {
        const double rate = m_rateWindowFrames * 1000.0 / elapsed;
        if (m_rateLabel) m_rateLabel->setText(QStringLiteral("%1 frame/s").arg(rate, 0, 'f', 1));
        if (m_rateSummaryCard) m_rateSummaryCard->setValue(rate, 1);
        m_rateWindowStart = now.toMSecsSinceEpoch();
        m_rateWindowFrames = 0;
    }
    if (m_lastFrameAt > 0 && now.toMSecsSinceEpoch() - m_lastFrameAt > 10000 && !m_clients.isEmpty())
        m_connectionDetail->setText(QStringLiteral("设备在线，但超过 10 秒未收到新数据"));
}
