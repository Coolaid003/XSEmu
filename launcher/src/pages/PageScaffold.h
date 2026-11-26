#pragma once

#include <QString>

class PageHeader;
class QVBoxLayout;
class QWidget;

// Every main page: the 64 px header and, under it, a scroll area whose content sits at the page
// inset. Callers add their sections to content and a stretch at the end.
struct PageScaffold {
    PageHeader* header;
    QVBoxLayout* content;
};

PageScaffold makePageScaffold(QWidget* page, const QString& title);

// "ALL GAMES"-style section label with an optional trailing widget on the right.
QWidget* makeSectionLabel(const QString& text, QWidget* trailing = nullptr);
