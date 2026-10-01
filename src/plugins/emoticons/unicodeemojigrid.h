#ifndef UNICODEEMOJIGRID_H
#define UNICODEEMOJIGRID_H

#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class UnicodeEmojiGrid : public QWidget
{
    Q_OBJECT
public:
    explicit UnicodeEmojiGrid(QWidget *AParent = nullptr);

    void setCategory(const QString &ACategory);
    QSize sizeHint() const override;

signals:
    void emojiSelected(const QString &AEmoji);

protected:
    void leaveEvent(QEvent *AEvent) override;
    void mouseMoveEvent(QMouseEvent *AEvent) override;
    void mousePressEvent(QMouseEvent *AEvent) override;
    void paintEvent(QPaintEvent *AEvent) override;
    void resizeEvent(QResizeEvent *AEvent) override;

private:
    struct Entry
    {
        QString emoji;
        QString tooltip;
    };

    int entryAt(const QPoint &APosition) const;
    void updateGeometryForEntries();

    QList<Entry> FEntries;
    int FHoveredEntry = -1;
    int FCellSize = 38;
    int FColumns = 8;
};

#endif // UNICODEEMOJIGRID_H
