#include "uilanguage.h"

#include <QTabBar>
#include <QComboBox>
#include <QSignalBlocker>

namespace UiLanguage {
int addTab(QTabBar *tabs, const char *source, const QIcon &icon) {
    const int index = tabs->addTab(icon, text(source));
    bind(tabs, "tab" + QByteArray::number(index), [tabs, index, source = QByteArray(source)] {
        tabs->setTabText(index, text(source.constData()));
    });
    return index;
}
void addItem(QComboBox *combo, const char *source, const QVariant &data) {
    const int index = combo->count();
    combo->addItem(text(source), data);
    bind(combo, "item" + QByteArray::number(index), [combo, index, source = QByteArray(source)] {
        const QSignalBlocker blocker(combo);
        combo->setItemText(index, text(source.constData()));
    });
}
}
