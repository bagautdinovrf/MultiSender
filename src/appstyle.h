#pragma once

#include <QProxyStyle>
#include <QSet>

// Native, palette-driven rendering. Geometry and fonts are configured once;
// changing the theme only changes brushes and schedules painting.
class AppStyle final : public QProxyStyle
{
public:
    AppStyle();

    using QProxyStyle::polish;
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    void refreshPalette(QWidget *widget);
    void refreshTheme();

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option,
                     QPainter *painter, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &contentsSize, const QWidget *widget = nullptr) const override;
    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget = nullptr) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                    const QPoint &position, const QWidget *widget = nullptr) const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void configureWidget(QWidget *widget);
    QSet<QWidget *> m_configured;
    QSet<QWidget *> m_paletteWidgets;
    QSet<QWidget *> m_surfaces;
    bool m_refreshingPalette = false;
};
