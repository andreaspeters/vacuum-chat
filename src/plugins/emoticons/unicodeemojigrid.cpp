#include "unicodeemojigrid.h"

#include "unicodeemoji_data.h"

#include <QMouseEvent>
#include <QFontDatabase>
#include <QPainter>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QToolTip>

#include <algorithm>

UnicodeEmojiGrid::UnicodeEmojiGrid(QWidget *AParent)
    : QWidget(AParent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void UnicodeEmojiGrid::setCategory(const QString &ACategory)
{
    FEntries.clear();
    FHoveredEntry = -1;

    for (int index = 0; index < UNICODE_EMOJI_ENTRY_COUNT; ++index) {
        const UnicodeEmojiEntry &entry = UNICODE_EMOJI_ENTRIES[index];
        if (QString::fromUtf8(entry.category) != ACategory)
            continue;

        const QString name = QString::fromUtf8(entry.name);
        FEntries.append({
            QString::fromUtf8(entry.emoji),
            QStringLiteral(":%1:").arg(
                name.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
                                       QStringLiteral("_")))});
    }

    updateGeometryForEntries();
    update();
}

QSize UnicodeEmojiGrid::sizeHint() const
{
    const int rows = (FEntries.size() + FColumns - 1) / FColumns;
    return QSize(FColumns * FCellSize, std::max(1, rows) * FCellSize);
}

int UnicodeEmojiGrid::entryAt(const QPoint &APosition) const
{
    if (APosition.x() < 0 || APosition.y() < 0)
        return -1;
    const int columns = std::max(1, width() / FCellSize);
    const int column = APosition.x() / FCellSize;
    const int row = APosition.y() / FCellSize;
    if (column >= columns)
        return -1;
    const int index = row * columns + column;
    return index >= 0 && index < FEntries.size() ? index : -1;
}

void UnicodeEmojiGrid::updateGeometryForEntries()
{
    FColumns = std::max(1, width() / FCellSize);
    const int rows = (FEntries.size() + FColumns - 1) / FColumns;
    setMinimumHeight(std::max(1, rows) * FCellSize);
    updateGeometry();
}

void UnicodeEmojiGrid::leaveEvent(QEvent *AEvent)
{
    FHoveredEntry = -1;
    QToolTip::hideText();
    update();
    QWidget::leaveEvent(AEvent);
}

void UnicodeEmojiGrid::mouseMoveEvent(QMouseEvent *AEvent)
{
    const int entry = entryAt(AEvent->position().toPoint());
    if (entry != FHoveredEntry) {
        FHoveredEntry = entry;
        if (entry >= 0)
            QToolTip::showText(AEvent->globalPosition().toPoint(), FEntries.at(entry).tooltip, this);
        else
            QToolTip::hideText();
        update();
    }
    QWidget::mouseMoveEvent(AEvent);
}

void UnicodeEmojiGrid::mousePressEvent(QMouseEvent *AEvent)
{
    if (AEvent->button() == Qt::LeftButton) {
        const int entry = entryAt(AEvent->position().toPoint());
        if (entry >= 0)
            emit emojiSelected(FEntries.at(entry).emoji);
    }
    QWidget::mousePressEvent(AEvent);
}

void UnicodeEmojiGrid::resizeEvent(QResizeEvent *AEvent)
{
    updateGeometryForEntries();
    QWidget::resizeEvent(AEvent);
}

void UnicodeEmojiGrid::paintEvent(QPaintEvent *AEvent)
{
    Q_UNUSED(AEvent);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int columns = std::max(1, width() / FCellSize);
    const QFont emojiFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    QFont drawFont = emojiFont;
    drawFont.setPointSize(20);
    painter.setFont(drawFont);

    for (int index = 0; index < FEntries.size(); ++index) {
        const int row = index / columns;
        const int column = index % columns;
        const QRect cell(column * FCellSize, row * FCellSize, FCellSize, FCellSize);
        if (index == FHoveredEntry) {
            QColor hover = palette().color(QPalette::Highlight);
            hover.setAlpha(70);
            painter.setBrush(hover);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(cell.adjusted(1, 1, -1, -1), 5, 5);
        }
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(cell, Qt::AlignCenter, FEntries.at(index).emoji);
    }
}
