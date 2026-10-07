#include <QAbstractButton>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QJsonArray>
#include <QMimeData>
#include <QSet>
#include <QStyle>
#include <QUuid>
#include <QClipboard>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QShortcut>
#include <utility>
#include <vector>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QSvgRenderer>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFutureWatcher>
#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFormat>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QStringDecoder>
#include <QtConcurrent/QtConcurrentRun>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
const QString kServerName = QStringLiteral("Pyrus.BetterNotepad.NativeQt6");
const QString kProductId = QStringLiteral("com.pyrus.better-notepad");

QString appDataDirectory() {
#ifdef Q_OS_WIN
    const QString root = qEnvironmentVariable("LOCALAPPDATA", QDir::homePath() + QStringLiteral("/AppData/Local"));
    const QString path = QDir(root).filePath(kProductId);
#else
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#endif
    QDir().mkpath(path);
    return path;
}

QString decodeText(const QByteArray &bytes) {
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QStringDecoder(QStringDecoder::Utf8)(QByteArrayView(bytes).sliced(3));

    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        const bool littleEndian = bytes.startsWith("\xFF\xFE");
        QByteArray utf16 = bytes.mid(2);
        if (utf16.size() % 2)
            utf16.chop(1);
        if (!littleEndian) {
            for (qsizetype i = 0; i + 1 < utf16.size(); i += 2)
                qSwap(utf16[i], utf16[i + 1]);
        }
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(utf16.constData()),
                                  utf16.size() / 2);
    }

    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(bytes);
    if (!decoder.hasError())
        return text;

#ifdef Q_OS_WIN
    const int length = MultiByteToWideChar(CP_ACP, 0, bytes.constData(),
                                            static_cast<int>(bytes.size()), nullptr, 0);
    if (length > 0) {
        std::wstring wide(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_ACP, 0, bytes.constData(),
                            static_cast<int>(bytes.size()), wide.data(), length);
        return QString::fromWCharArray(wide.data(), length);
    }
#endif
    return QString::fromUtf8(bytes);
}

qsizetype countWords(const QString &text) {
    qsizetype count = 0;
    QRegularExpressionMatchIterator matches =
        QRegularExpression(QStringLiteral("\\S+")).globalMatch(text);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    return count;
}

struct LoadedFile {
    QString path;
    QString text;
    QString error;
};

LoadedFile readFile(const QString &path) {
    LoadedFile result;
    result.path = path;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    result.text = decodeText(file.readAll());
    return result;
}

// Local regular files from a drag/drop payload (folders and web URLs are ignored).
QStringList localFilesFrom(const QMimeData *mime) {
    QStringList paths;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
            paths << url.toLocalFile();
    }
    return paths;
}

QString quotedName(const QString &path) {
    return path.isEmpty() ? QStringLiteral("Untitled.txt") : QFileInfo(path).fileName();
}
} // namespace

#include "skins.inc"

// "rgb(1,2,3)", "rgba(1,2,3,.5)", "#abc" or a CSS colour name -> QColor.
QColor cssColor(const QString &text) {
    static const QRegularExpression rgb(QStringLiteral("^rgba?\\(([^)]*)\\)$"));
    const QString s = text.trimmed();
    const auto match = rgb.match(s);
    if (match.hasMatch()) {
        const QStringList p = match.captured(1).split(QLatin1Char(','));
        if (p.size() >= 3) {
            QColor c(p[0].trimmed().toInt(), p[1].trimmed().toInt(), p[2].trimmed().toInt());
            if (p.size() > 3)
                c.setAlphaF(p[3].trimmed().toDouble());
            return c;
        }
    }
    return QColor(s);
}

QColor withAlpha(QColor c, int alpha) {
    c.setAlpha(alpha);
    return c;
}

QString css(const QColor &c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(c.alphaF(), 0, 'f', 3);
}

// The Tauri skin variables (--text-color, --tab-bg, ...) as colours. Built-in skins come from
// skins.inc; custom ones are <appdata>/skins/<name>.json with a "vars" object, same as Tauri.
class Skin {
public:
    static QStringList builtInNames() {
        QStringList names;
        for (const SkinDef &def : kBuiltInSkins)
            names << QLatin1String(def.name);
        return names;
    }

    static QStringList customNames(const QString &dir) {
        QStringList names;
        for (const QString &file : QDir(dir).entryList({QStringLiteral("*.json")}, QDir::Files))
            names << QFileInfo(file).completeBaseName();
        return names;
    }

    static Skin load(const QString &name, const QString &dir) {
        Skin skin;
        skin.apply(kBuiltInSkins.front());
        for (const SkinDef &def : kBuiltInSkins) {
            if (name == QLatin1String(def.name)) {
                skin.apply(def);
                return skin;
            }
        }
        QFile file(QDir(dir).filePath(name + QStringLiteral(".json")));
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject vars = QJsonDocument::fromJson(file.readAll()).object()
                                         .value(QStringLiteral("vars")).toObject();
            for (auto it = vars.begin(); it != vars.end(); ++it)
                skin.m_colors.insert(it.key(), cssColor(it.value().toString()));
        }
        return skin;
    }

    QColor operator()(const char *key) const { return m_colors.value(QLatin1String(key)); }

private:
    void apply(const SkinDef &def) {
        for (const auto &var : def.vars)
            m_colors.insert(QLatin1String(var.first), cssColor(QLatin1String(var.second)));
    }
    QHash<QString, QColor> m_colors;
};

class Editor;

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(Editor *editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Editor *m_editor;
};

// Thin strip over the vertical scrollbar that marks where the find matches are.
class MatchMap final : public QWidget {
public:
    explicit MatchMap(Editor *editor);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Editor *m_editor;
};

class Editor final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr)
        : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)),
          m_matchMap(new MatchMap(this)) {
        setFrameStyle(QFrame::NoFrame);
        updateTabStop();
        connect(this, &QPlainTextEdit::blockCountChanged,
                this, &Editor::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest,
                this, &Editor::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged,
                this, &Editor::highlightCurrentLine);
        // The scrollbar shows/hides as the document grows, which doesn't always trigger a resize.
        connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this] { placeMatchMap(); });
        updateLineNumberAreaWidth();
        highlightCurrentLine();
    }

    QString path;
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces); // names its .bak file
    int backupRevision = -1; // document revision last written to the session backup
    bool dirty = false;
    bool wordWrap = false;
    int zoomPercent = 100;
    qsizetype wordCount = -1;
    quint64 wordCountGeneration = 0;

    int lineNumberAreaWidth() const {
        if (!m_showLineNumbers)
            return 0;
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return 12 + QFontMetrics(font()).horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void setLineNumbersVisible(bool visible) {
        m_showLineNumbers = visible;
        updateLineNumberAreaWidth();
        m_lineNumberArea->update();
    }

    bool lineNumbersVisible() const { return m_showLineNumbers; }

    // Highlights every match of `regex` and marks them on the scrollbar.
    // A null/invalid regex clears both.
    void setFindHighlight(const QRegularExpression &regex, const QColor &color) {
        m_findColor = color;
        m_matchPositions.clear();
        m_searchSelections.clear();
        if (regex.isValid() && !regex.pattern().isEmpty()) {
            QRegularExpressionMatchIterator it = regex.globalMatch(toPlainText());
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                if (match.capturedLength() == 0)
                    continue;
                m_matchPositions.append(match.capturedStart());
                // ponytail: only the first 2000 matches get a text highlight (ExtraSelections get slow);
                // the scrollbar map always shows all of them. Highlight just the visible range if needed.
                if (m_searchSelections.size() < 2000) {
                    QTextEdit::ExtraSelection selection;
                    selection.format.setBackground(color);
                    selection.cursor = QTextCursor(document());
                    selection.cursor.setPosition(static_cast<int>(match.capturedStart()));
                    selection.cursor.setPosition(static_cast<int>(match.capturedEnd()), QTextCursor::KeepAnchor);
                    m_searchSelections.append(selection);
                }
            }
        }
        highlightCurrentLine();
        m_matchMap->update();
    }

    // The map sits exactly behind the vertical scrollbar. The scrollbar track is transparent and its
    // handle semi-transparent (see applyTheme), so the ticks show through like VS Code's overview ruler.
    void placeMatchMap() {
        QWidget *bar = verticalScrollBar()->parentWidget();
        m_matchMap->setGeometry(bar->geometry());
        m_matchMap->setVisible(!bar->isHidden());
        m_matchMap->stackUnder(bar);
    }

    void setTrackColor(const QColor &color) {
        m_trackColor = color;
        m_matchMap->update();
    }

    void paintMatchMap(QPainter &painter, int height) {
        painter.fillRect(m_matchMap->rect(), m_trackColor);
        if (m_matchPositions.isEmpty())
            return;
        QColor color = m_findColor;
        color.setAlpha(230);
        const double lines = qMax(1, document()->lineCount());
        for (qsizetype pos : std::as_const(m_matchPositions)) {
            const QTextBlock block = document()->findBlock(static_cast<int>(pos));
            if (!block.isValid())
                continue;
            const int y = static_cast<int>(block.firstLineNumber() / lines * height);
            painter.fillRect(QRect(0, y, m_matchMap->width(), 2), color);
        }
    }

    void setGutterColors(const QColor &background, const QColor &foreground, const QColor &border) {
        m_gutterBg = background;
        m_gutterFg = foreground;
        m_gutterBorder = border;
        m_lineNumberArea->update();
    }

    void setWordWrapEnabled(bool enabled) {
        wordWrap = enabled;
        setLineWrapMode(enabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    }

    void setSearchSelections(const QList<QTextEdit::ExtraSelection> &selections) {
        m_searchSelections = selections;
        highlightCurrentLine();
    }

    // 0 = the Tab key types a real tab character; N = it types N spaces. Tabs are drawn N (or 4) columns wide.
    void setTabSpaces(int spaces) {
        m_tabSpaces = spaces;
        updateTabStop();
    }

    void updateTabStop() {
        setTabStopDistance(QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')) * (m_tabSpaces ? m_tabSpaces : 4));
    }

    void setZoomPercent(int percent) {
        zoomPercent = qBound(50, percent, 300);
        QFont editorFont = font();
        editorFont.setPointSizeF(12.0 * zoomPercent / 100.0);
        setFont(editorFont);
        updateTabStop();
        updateLineNumberAreaWidth();
    }

    void setEditorFont(const QFont &font) {
        QFont updated = font;
        updated.setPointSizeF(qMax(6.0, font.pointSizeF() * zoomPercent / 100.0));
        QPlainTextEdit::setFont(updated);
        updateTabStop();
        updateLineNumberAreaWidth();
    }

    void paintLineNumberArea(QPaintEvent *event) {
        QPainter painter(m_lineNumberArea);
        const QColor gutterBackground = m_gutterBg;
        const QColor textColor = palette().color(QPalette::Text);
        const QColor inactiveColor = m_gutterFg;
        painter.fillRect(event->rect(), gutterBackground);
        painter.fillRect(QRect(m_lineNumberArea->width() - 1, event->rect().top(), 1, event->rect().height()),
                         m_gutterBorder);

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());
        const int current = textCursor().blockNumber();

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                painter.setPen(blockNumber == current ? textColor : inactiveColor);
                QFont numberFont = painter.font();
                numberFont.setBold(blockNumber == current);
                painter.setFont(numberFont);
                painter.drawText(QRect(0, top, m_lineNumberArea->width() - 6,
                                       qMax(bottom - top, fontMetrics().height())),
                                 Qt::AlignRight | Qt::AlignVCenter,
                                 QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

signals:
    void zoomWheel(int delta);
    void filesDropped(const QStringList &paths);

protected:
    // Files dragged from Explorer open as tabs; plain text drags still insert text as usual.
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
        else
            QPlainTextEdit::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
        else
            QPlainTextEdit::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override {
        if (event->mimeData()->hasUrls()) {
            emit filesDropped(localFilesFrom(event->mimeData()));
            event->acceptProposedAction();
        } else {
            QPlainTextEdit::dropEvent(event);
        }
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (m_tabSpaces > 0 && event->key() == Qt::Key_Tab && !isReadOnly() &&
            !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            QTextCursor cursor = textCursor();
            cursor.insertText(QString(m_tabSpaces, QLatin1Char(' ')));
            setTextCursor(cursor);
            return;
        }
        if (event->matches(QKeySequence::Cut) && !textCursor().hasSelection() && !isReadOnly()) {
            const QTextBlock block = textCursor().block();
            QTextCursor cursor(document());
            if (block.next().isValid()) { // take the line break with it
                cursor.setPosition(block.position());
                cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
            } else if (block.previous().isValid()) { // last line: take the break before it instead
                cursor.setPosition(block.previous().position() + block.previous().length() - 1);
                cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            } else {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
            }
            QApplication::clipboard()->setText(block.text() + QLatin1Char('\n'));
            cursor.removeSelectedText();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override {
        if (event->modifiers() & Qt::ControlModifier) {
            if (event->angleDelta().y() != 0)
                emit zoomWheel(event->angleDelta().y() > 0 ? 1 : -1); // direction; the step is a setting
            event->accept();
            return;
        }
        QPlainTextEdit::wheelEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override {
        QPlainTextEdit::resizeEvent(event);
        const QRect contents = contentsRect();
        m_lineNumberArea->setGeometry(QRect(contents.left(), contents.top(),
                                            lineNumberAreaWidth(), contents.height()));
        placeMatchMap();
    }

private slots:
    void updateLineNumberAreaWidth() {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect &rect, int dy) {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth();
    }

    void highlightCurrentLine() {
        QList<QTextEdit::ExtraSelection> selections = m_searchSelections;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection currentLine;
            currentLine.format.setBackground(palette().alternateBase());
            currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
            currentLine.cursor = textCursor();
            currentLine.cursor.clearSelection();
            selections.prepend(currentLine);
        }
        setExtraSelections(selections);
        m_lineNumberArea->update();
    }

private:
    LineNumberArea *m_lineNumberArea;
    bool m_showLineNumbers = true;
    MatchMap *m_matchMap;
    int m_tabSpaces = 0;
    QList<qsizetype> m_matchPositions;
    QColor m_findColor{158, 106, 3, 190};
    QColor m_trackColor{25, 25, 25};
    QColor m_gutterBg{28, 28, 28};
    QColor m_gutterFg{136, 136, 136};
    QColor m_gutterBorder{40, 40, 40};
    QList<QTextEdit::ExtraSelection> m_searchSelections;
};

MatchMap::MatchMap(Editor *editor) : QWidget(editor), m_editor(editor) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void MatchMap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    m_editor->paintMatchMap(painter, height());
}

LineNumberArea::LineNumberArea(Editor *editor)
    : QWidget(editor), m_editor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(m_editor->lineNumberAreaWidth(), 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    m_editor->paintLineNumberArea(event);
}

// Material icon paths shared with the Tauri settings window (viewBox 0 -960 960 960).
namespace icons {
const char *const skin = "M346-140 100-386q-10-10-15-22t-5-25q0-13 5-25t15-22l230-229-106-106 62-65 400 400q10 10 14.5 22t4.5 25q0 13-4.5 25T686-386L440-140q-10 10-22 15t-25 5q-13 0-25-5t-22-15Zm47-506L179-432h428L393-646Zm399 526q-36 0-61-25.5T706-208q0-27 13.5-51t30.5-47l42-54 44 54q16 23 30 47t14 51q0 37-26 62.5T792-120Z";
const char *const font = "m247-364-32 89q-4 11-13 17t-20 6q-19 0-29.5-15.5T149-300l138-368q4-11 13.5-17.5T321-692h28q11 0 21 6.5t14 17.5l138 369q7 17-4 32t-29 15q-11 0-20-6.5T456-276l-31-88H247Zm23-64h131l-64-182h-4l-63 182Zm395 186q-51 0-81-27.5T554-342q0-44 34.5-72.5T677-443q23 0 45 4t38 11v-12q0-29-20.5-47T685-505q-15 0-29.5 4.5T629-487q-13 10-24.5 7T586-491q-7-8-7-19t11-19q20-16 45-23.5t51-7.5q69 0 103 32.5t34 97.5v147q0 13-9.5 22t-22.5 9q-13 0-22-9.5t-9-22.5v-5h-4q-14 23-38 35t-53 12Zm12-54q35 0 59.5-24t24.5-56q-14-8-33.5-12.5T689-393q-32 0-50 14t-18 37q0 20 16 33t40 13Z";
const char *const wrap = "M588-132 440-280l148-148 56 58-50 50h96q29 0 49.5-20.5T760-390q0-29-20.5-49.5T690-460H160v-80h530q63 0 106.5 43.5T840-390q0 63-43.5 106.5T690-240h-96l50 50-56 58ZM160-240v-80h200v80H160Zm0-440v-80h640v80H160Z";
const char *const numbers = "M120-80v-60h100v-30h-60v-60h60v-30H120v-60h120q17 0 28.5 11.5T280-280v40q0 17-11.5 28.5T240-200q17 0 28.5 11.5T280-160v40q0 17-11.5 28.5T240-80H120Zm0-280v-110q0-17 11.5-28.5T160-510h60v-30H120v-60h120q17 0 28.5 11.5T280-560v70q0 17-11.5 28.5T240-450h-60v30h100v60H120Zm60-280v-180h-60v-60h120v240h-60Zm180 440v-80h480v80H360Zm0-240v-80h480v80H360Zm0-240v-80h480v80H360Z";
const char *const statusbar = "M200-120q-33 0-56.5-23.5T120-200v-560q0-33 23.5-56.5T200-840h560q33 0 56.5 23.5T840-760v560q0 33-23.5 56.5T760-120H200Zm0-240h560v-400H200v400Zm0 80v80h560v-80H200Zm0 0v80-80Z";
const char *const zoom = "M380-320q-109 0-184.5-75.5T120-580q0-109 75.5-184.5T380-840q109 0 184.5 75.5T640-580q0 44-14 83t-38 69l224 224q11 11 11 28t-11 28q-11 11-28 11t-28-11L532-372q-30 24-69 38t-83 14Zm0-80q75 0 127.5-52.5T560-580q0-75-52.5-127.5T380-760q-75 0-127.5 52.5T200-580q0 75 52.5 127.5T380-400Z";
const char *const tabKey = "M740-240v-480h80v480h-80ZM480-240l-56-58 142-142H120v-80h446L424-662l56-58 240 240-240 240Z";
const char *const history = "M480-120q-126 0-223-76.5T131-392q-4-15 6-27.5t27-14.5q16-2 29 6t18 24q24 90 99 147t170 57q117 0 198.5-81.5T760-480q0-117-81.5-198.5T480-760q-69 0-129 32t-101 88h70q17 0 28.5 11.5T360-600q0 17-11.5 28.5T320-560H160q-17 0-28.5-11.5T120-600v-160q0-17 11.5-28.5T160-800q17 0 28.5 11.5T200-760v54q51-64 124.5-99T480-840q75 0 140.5 28.5t114 77q48.5 48.5 77 114T840-480q0 75-28.5 140.5t-77 114q-48.5 48.5-114 77T480-120Zm40-376 100 100q11 11 11 28t-11 28q-11 11-28 11t-28-11L452-452q-6-6-9-13.5t-3-15.5v-159q0-17 11.5-28.5T480-680q17 0 28.5 11.5T520-640v144Z";
const char *const find = "M120-200q-17 0-28.5-11.5T80-240q0-17 11.5-28.5T120-280h320q17 0 28.5 11.5T480-240q0 17-11.5 28.5T440-200H120Zm0-200q-17 0-28.5-11.5T80-440q0-17 11.5-28.5T120-480h120q17 0 28.5 11.5T280-440q0 17-11.5 28.5T240-400H120Zm0-200q-17 0-28.5-11.5T80-640q0-17 11.5-28.5T120-680h120q17 0 28.5 11.5T280-640q0 17-11.5 28.5T240-600H120Zm440 280q-83 0-141.5-58.5T360-520q0-83 58.5-141.5T560-720q83 0 141.5 58.5T760-520q0 29-8.5 57.5T726-410l126 126q11 11 11 28t-11 28q-11 11-28 11t-28-11L670-354q-24 17-52.5 25.5T560-320Zm0-80q50 0 85-35t35-85q0-50-35-85t-85-35q-50 0-85 35t-35 85q0 50 35 85t85 35Z";
// GitHub mark, 16x16 box (see svgPixmap viewBox argument).
const char *const github = "M8 0C3.58 0 0 3.58 0 8C0 11.54 2.29 14.53 5.47 15.59C5.87 15.66 6.02 15.42 6.02 15.21C6.02 15.02 6.01 14.39 6.01 13.72C4 14.09 3.48 13.23 3.32 12.78C3.23 12.55 2.84 11.84 2.5 11.65C2.22 11.5 1.82 11.13 2.49 11.12C3.12 11.11 3.57 11.7 3.72 11.94C4.44 13.15 5.59 12.81 6.05 12.6C6.12 12.08 6.33 11.73 6.56 11.53C4.78 11.33 2.92 10.64 2.92 7.58C2.92 6.71 3.23 5.99 3.74 5.43C3.66 5.23 3.38 4.41 3.82 3.31C3.82 3.31 4.49 3.1 6.02 4.13C6.66 3.95 7.34 3.86 8.02 3.86C8.7 3.86 9.38 3.95 10.02 4.13C11.55 3.09 12.22 3.31 12.22 3.31C12.66 4.41 12.38 5.23 12.3 5.43C12.81 5.99 13.12 6.7 13.12 7.58C13.12 10.65 11.25 11.33 9.47 11.53C9.76 11.78 10.01 12.26 10.01 13.01C10.01 14.08 10 14.94 10 15.21C10 15.42 10.15 15.67 10.55 15.59C13.71 14.53 16 11.53 16 8C16 3.58 12.42 0 8 0Z";
} // namespace icons

QPixmap svgPixmap(const char *path, const QColor &color, int size, const char *viewBox = "0 -960 960 960") {
    const QString svg = QStringLiteral(
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='%3'><path fill='%1' d='%2'/></svg>")
        .arg(color.name(), QLatin1String(path), QLatin1String(viewBox));
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

#include "menu_icons.inc"

// Renders a complete <svg> whose paint is "currentColor" in the given colour.
QPixmap svgMarkupPixmap(QString svg, const QColor &color, int size) {
    svg.replace(QLatin1String("currentColor"), color.name());
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pixmap(size * 2, size * 2);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRectF(0, 0, size, size));
    return pixmap;
}

// Pill switch, same proportions as the .switch/.slider CSS (60x34 at zoom 80%).
class ToggleSwitch final : public QAbstractButton {
public:
    explicit ToggleSwitch(bool on, QWidget *parent = nullptr) : QAbstractButton(parent) {
        setCheckable(true);
        setChecked(on);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(48, 27);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(isChecked() ? m_on : m_off);
        p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
        const int d = 21;
        const int x = isChecked() ? width() - d - 3 : 3;
        p.setBrush(m_knob);
        p.drawEllipse(QRect(x, (height() - d) / 2, d, d));
    }

public:
    // --slider-bg / --slider-bg-checked / --text-color of the active skin.
    void setColors(const QColor &off, const QColor &on, const QColor &knob) {
        m_off = off;
        m_on = on;
        m_knob = knob;
        update();
    }

private:
    QColor m_off{204, 204, 204}, m_on{75, 75, 75}, m_knob{Qt::white};
};

// Checkbox that draws its own box and tick in the skin colours (a stylesheet indicator has no
// checkmark without an image file).
class TickCheckBox final : public QCheckBox {
public:
    using QCheckBox::QCheckBox;

    void setColors(const QColor &background, const QColor &border, const QColor &on, const QColor &text,
                   const QColor &dim) {
        m_bg = background;
        m_border = border;
        m_on = on;
        m_text = text;
        m_dim = dim;
        update();
    }

    QSize sizeHint() const override {
        return QSize(15 + 9 + fontMetrics().horizontalAdvance(text()), 20);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box(0.5, (height() - 15) / 2.0 + 0.5, 14, 14);
        p.setPen(QPen(isChecked() ? m_text : m_border, 1));
        p.setBrush(isChecked() ? m_on : m_bg);
        p.drawRoundedRect(box, 3, 3);
        if (isChecked()) {
            p.setPen(QPen(m_text, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            const QPointF o = box.topLeft();
            p.drawPolyline(QPolygonF{o + QPointF(3.3, 7.2), o + QPointF(6, 9.9), o + QPointF(10.8, 4.2)});
        }
        p.setPen(isEnabled() ? m_text : m_dim);
        p.drawText(QRectF(15 + 9, 0, width() - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }

private:
    QColor m_bg{27, 27, 27}, m_border{51, 51, 51}, m_on{75, 75, 75}, m_text{Qt::white}, m_dim{157, 157, 157};
};

// Frameless settings window; the top strip drags it like a title bar.
class SettingsDialog final : public QDialog {
public:
    using QDialog::QDialog;

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && event->position().y() < 34 && windowHandle()) {
            windowHandle()->startSystemMove();
            return;
        }
        QDialog::mousePressEvent(event);
    }
};

// Pages plus a tab bar that is NOT owned by this widget: the bar is handed to
// the title row so tabs, menu button and window buttons share one line.
class TabDeck final : public QWidget {
    Q_OBJECT
public:
    explicit TabDeck(QWidget *parent = nullptr)
        : QWidget(parent), m_bar(new QTabBar(parent)), m_stack(new QStackedWidget(this)) {
        m_bar->setDocumentMode(true);
        m_bar->setMovable(true);
        m_bar->setDrawBase(false);
        m_bar->setExpanding(false);
        m_bar->setFocusPolicy(Qt::NoFocus);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_stack);
        connect(m_bar, &QTabBar::currentChanged, this, [this](int index) {
            m_stack->setCurrentIndex(index);
            emit currentChanged(index);
        });
        connect(m_bar, &QTabBar::tabMoved, this, [this](int from, int to) {
            QWidget *page = m_stack->widget(from);
            m_stack->removeWidget(page);
            m_stack->insertWidget(to, page);
            emit tabMoved(from, to);
        });
    }

    QTabBar *tabBar() const { return m_bar; }
    int count() const { return m_bar->count(); }
    int currentIndex() const { return m_bar->currentIndex(); }
    QWidget *widget(int index) const { return m_stack->widget(index); }
    QWidget *currentWidget() const { return m_stack->currentWidget(); }
    int indexOf(QWidget *page) const { return m_stack->indexOf(page); }
    void setCurrentIndex(int index) { m_bar->setCurrentIndex(index); }
    void setCurrentWidget(QWidget *page) { setCurrentIndex(indexOf(page)); }
    void setTabText(int index, const QString &text) { m_bar->setTabText(index, text); }
    void setTabToolTip(int index, const QString &tip) { m_bar->setTabToolTip(index, tip); }

    int addTab(QWidget *page, const QString &label) {
        const int index = m_stack->addWidget(page);
        m_bar->insertTab(index, label);
        return index;
    }

    void removeTab(int index) {
        if (QWidget *page = m_stack->widget(index))
            m_stack->removeWidget(page);
        m_bar->removeTab(index);
    }

signals:
    void currentChanged(int index);
    void tabMoved(int from, int to);

private:
    QTabBar *m_bar;
    QStackedWidget *m_stack;
};

class BetterNotepad final : public QMainWindow {
    Q_OBJECT
public:
    explicit BetterNotepad(QStringList files)
        : m_settings(appDataDirectory() + QStringLiteral("/settings.ini")) {
        QApplication::setStyle(QStringLiteral("Fusion"));
        setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
        setAcceptDrops(true);
        setWindowTitle(QStringLiteral("Better Notepad"));
        setMinimumSize(520, 360);
        resize(m_settings.value(QStringLiteral("window/size"), QSize(900, 640)).toSize());
        if (m_settings.contains(QStringLiteral("window/position")))
            move(m_settings.value(QStringLiteral("window/position")).toPoint());

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        setCentralWidget(central);

        createActions();
        createMenus();

        m_tabs = new TabDeck(central);
        m_tabs->tabBar()->installEventFilter(this);
        createTitleBar(layout);
        createFindBar();
        createGoToBar();
        m_findHighlightTimer.setSingleShot(true);
        connect(&m_findHighlightTimer, &QTimer::timeout, this, [this] { updateFindHighlights(); });
        m_sessionTimer.setSingleShot(true);
        connect(&m_sessionTimer, &QTimer::timeout, this, [this] { saveSession(); });
        layout->addWidget(m_tabs, 1);
        connect(m_tabs, &TabDeck::currentChanged, this, [this] {
            updateStatus();
            updateFindHighlights();
            scheduleSessionSave();
        });
        connect(m_tabs, &TabDeck::tabMoved, this, [this](int, int) {
            updateStatus();
            scheduleSessionSave();
        });

        m_statusPath = new QLabel(this);
        m_statusCursor = new QLabel(this);
        m_statusLines = new QLabel(this);
        m_statusChars = new QLabel(this);
        m_statusWords = new QLabel(this);
        m_statusZoom = new QLabel(this);
        statusBar()->addWidget(m_statusPath, 1);
        statusBar()->addPermanentWidget(m_statusCursor);
        statusBar()->addPermanentWidget(m_statusChars);
        statusBar()->addPermanentWidget(m_statusWords);
        statusBar()->addPermanentWidget(m_statusZoom);
        auto *zoomReset = new QToolButton(this);
        zoomReset->setObjectName(QStringLiteral("statusBtn"));
        zoomReset->setText(QStringLiteral("⟳"));
        zoomReset->setToolTip(QStringLiteral("Reset Zoom (Ctrl+0)"));
        zoomReset->setAutoRaise(true);
        zoomReset->setFocusPolicy(Qt::NoFocus);
        connect(zoomReset, &QToolButton::clicked, this, [this] { setZoom(m_defaultZoom); });
        m_zoomReset = zoomReset;
        statusBar()->addPermanentWidget(zoomReset);
        statusBar()->addPermanentWidget(m_statusLines);
        statusBar()->setVisible(m_settings.value(QStringLiteral("view/statusBar"), true).toBool());
        applySettings();

        connect(&m_server, &QLocalServer::newConnection,
                this, &BetterNotepad::readSecondInstance);
        QLocalServer::removeServer(kServerName);
        m_server.listen(kServerName);

        QTimer::singleShot(0, this, [this, files = std::move(files)] {
            const bool restored = restoreSession();
            if (!files.isEmpty())
                openFiles(files);
            else if (!restored)
                newDocument();
        });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // Empty space in the title row behaves like a system title bar.
        if (watched == m_titleBar) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton)
                    toggleMaximized();
                return true;
            }
            if (event->type() == QEvent::MouseButtonPress &&
                static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && windowHandle()) {
                windowHandle()->startSystemMove();
                return true;
            }
        }
        if (watched == m_tabs->tabBar()) {
            auto *bar = m_tabs->tabBar();
            if (event->type() == QEvent::MouseButtonRelease) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::MiddleButton) {
                    const int index = bar->tabAt(mouse->position().toPoint());
                    if (index >= 0) {
                        closeTab(index);
                        return true;
                    }
                }
            } else if (event->type() == QEvent::MouseButtonDblClick) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton &&
                    bar->tabAt(mouse->position().toPoint()) < 0) {
                    toggleMaximized();
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonPress) {
                auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::LeftButton &&
                    bar->tabAt(mouse->position().toPoint()) < 0 && windowHandle()) {
                    windowHandle()->startSystemMove();
                    return true;
                }
            }
        }
        return QMainWindow::eventFilter(watched, event);
    }

    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        placePanels();
    }

    // Files dropped anywhere on the window (title bar, status bar...) open as tabs.
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls())
            event->acceptProposedAction();
    }

    void dropEvent(QDropEvent *event) override {
        openFiles(localFilesFrom(event->mimeData()));
        event->acceptProposedAction();
    }

    void changeEvent(QEvent *event) override {
        QMainWindow::changeEvent(event);
        if (event->type() == QEvent::WindowStateChange)
            updateTitleIcons();
    }

    void closeEvent(QCloseEvent *event) override {
        bool discarded = false; // user chose Discard: unsaved text must not come back next launch
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
            if (editor && editor->dirty) {
                QMessageBox box(QMessageBox::Warning, QStringLiteral("Unsaved changes"),
                                QStringLiteral("There are unsaved changes. Save before closing?"),
                                QMessageBox::NoButton, this);
                auto *save = box.addButton(QStringLiteral("Save All"), QMessageBox::AcceptRole);
                auto *discard = box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
                auto *cancel = box.addButton(QMessageBox::Cancel);
                box.setDefaultButton(qobject_cast<QPushButton *>(save));
                box.exec();
                if (box.clickedButton() == cancel) {
                    event->ignore();
                    return;
                }
                if (box.clickedButton() == save) {
                    if (!saveAll()) {
                        event->ignore();
                        return;
                    }
                } else if (box.clickedButton() != discard) {
                    event->ignore();
                    return;
                }
                discarded = box.clickedButton() == discard;
                break;
            }
        }

        saveSession(discarded);
        m_settings.setValue(QStringLiteral("window/size"), size());
        m_settings.setValue(QStringLiteral("window/position"), pos());
        event->accept();
    }

private:
    QSettings m_settings;
    QLocalServer m_server;
    TabDeck *m_tabs = nullptr;
    QWidget *m_titleBar = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_minimizeButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLineEdit *m_replaceInput = nullptr;
    QLabel *m_findResult = nullptr;
    QLabel *m_statusPath = nullptr;
    QLabel *m_statusCursor = nullptr;
    QLabel *m_statusLines = nullptr;
    QLabel *m_statusChars = nullptr;
    QLabel *m_statusWords = nullptr;
    QLabel *m_statusZoom = nullptr;
    QTimer m_wordCountTimer;
    QTimer m_findHighlightTimer;
    QTimer m_sessionTimer;
    bool m_restoreSession = false;
    int m_defaultZoom = 100; // zoom of new tabs and the target of Ctrl+0 / the reset button
    int m_zoomStep = 10;
    int m_tabSpaces = 0;     // 0 = Tab key types a tab character
    QToolButton *m_zoomReset = nullptr;
    // Which status bar items are shown.
    struct StatusItems { bool path = true, cursor = true, chars = true, words = true, zoom = true, lines = true; };
    StatusItems m_status;
    QAction *m_reopenAction = nullptr;

    // A closed tab, kept so Ctrl+Shift+T can bring it back (with unsaved text if it had any).
    struct ClosedTab {
        QString path;
        QString text;
        bool hasText = false;
        bool dirty = false;
        int zoom = 100;
        int cursor = 0;
    };
    QList<ClosedTab> m_closedTabs;
    QStringList m_openQueue;
    QPointer<Editor> m_loadingEditor;
    QHash<Editor *, QFutureWatcher<LoadedFile> *> m_loadWatchers;
    QHash<Editor *, QFutureWatcher<qsizetype> *> m_wordWatchers;
    QFont m_baseFont = QFont(QStringLiteral("Consolas"), 10);
    bool m_selectResults = true;
    QString m_skin = QStringLiteral("dark");
    bool m_wordWrap = false;
    bool m_lineNumbers = true;

    static constexpr int kTitleBarHeight = 34;

    QToolButton *createTitleButton(const QString &glyph, const QString &tip,
                                   int width, int fontSize, bool danger = false) {
        auto *button = new QToolButton(m_titleBar);
        button->setText(glyph);
        button->setToolTip(tip);
        button->setFixedSize(width, kTitleBarHeight);
        button->setFocusPolicy(Qt::NoFocus);
        button->setObjectName(danger ? QStringLiteral("titleClose") : QStringLiteral("titleBtn"));
        QFont font = button->font();
        font.setPixelSize(fontSize);
        button->setFont(font);
        return button;
    }

    void createTitleBar(QVBoxLayout *layout) {
        m_titleBar = new QWidget(centralWidget());
        m_titleBar->setFixedHeight(kTitleBarHeight);
        m_titleBar->installEventFilter(this);
        auto *row = new QHBoxLayout(m_titleBar);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);

        menuBar()->hide();

        auto *menuButton = createTitleButton(QStringLiteral("☰"), QStringLiteral("Menu"), 38, 17);
        connect(menuButton, &QToolButton::clicked, this, [this, menuButton] {
            // File / Edit / View / Help submenus built from the (hidden) menu bar, with skin-coloured icons.
            auto *menu = new QMenu(this);
            menu->setAttribute(Qt::WA_DeleteOnClose);
            for (QAction *menuAction : menuBar()->actions()) {
                if (!menuAction->menu())
                    continue;
                QMenu *submenu = menu->addMenu(menuAction->text().remove(QLatin1Char('&')));
                submenu->setIcon(menuIcon(menuAction->text().remove(QLatin1Char('&'))));
                for (QAction *action : menuAction->menu()->actions()) {
                    if (!action->isSeparator())
                        action->setIcon(menuIcon(action->text()));
                    submenu->addAction(action);
                }
            }
            menu->popup(menuButton->mapToGlobal(QPoint(0, menuButton->height())));
        });

        QTabBar *bar = m_tabs->tabBar();
        bar->setParent(m_titleBar);
        bar->setFixedHeight(kTitleBarHeight);

        auto *minimizeButton = m_minimizeButton = createTitleButton(QString(), QStringLiteral("Minimize"), 46, 13);
        connect(minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);

        m_maximizeButton = createTitleButton(QString(), QStringLiteral("Maximize"), 46, 13);
        connect(m_maximizeButton, &QToolButton::clicked, this, [this] { toggleMaximized(); });

        auto *closeButton = m_closeButton = createTitleButton(QString(), QStringLiteral("Close window"), 46, 13, true);
        connect(closeButton, &QToolButton::clicked, this, &QWidget::close);

        row->addWidget(menuButton);
        auto *newTabButton = createTitleButton(QStringLiteral("+"), QStringLiteral("New tab"), 30, 18);
        connect(newTabButton, &QToolButton::clicked, this, [this] { newDocument(); });

        row->addWidget(bar);
        row->addWidget(newTabButton);
        row->addStretch(1);
        row->addWidget(minimizeButton);
        row->addWidget(m_maximizeButton);
        row->addWidget(closeButton);
        layout->addWidget(m_titleBar);
    }

    // Icon for a menu entry, matched on its label; empty icon for anything unknown.
    QIcon menuIcon(const QString &label) const {
        static const QHash<QString, const char *> byLabel = {
            {QStringLiteral("File"), kMenuIconOpen},
            {QStringLiteral("Edit"), kMenuIconFind},
            {QStringLiteral("View"), kMenuIconOpenLocation},
            {QStringLiteral("Help"), kMenuIconAbout},
            {QStringLiteral("New"), kMenuIconNewFile},
            {QStringLiteral("Open…"), kMenuIconOpen},
            {QStringLiteral("Reopen Closed Tab"), kMenuIconOpen},
            {QStringLiteral("Save"), kMenuIconSave},
            {QStringLiteral("Save As…"), kMenuIconSaveAs},
            {QStringLiteral("Save All"), kMenuIconSaveAll},
            {QStringLiteral("Print…"), kMenuIconPrint},
            {QStringLiteral("Exit"), kMenuIconAbout},
            {QStringLiteral("Find"), kMenuIconFind},
            {QStringLiteral("Replace"), kMenuIconFind},
            {QStringLiteral("Go to Line…"), kMenuIconGoTo},
            {QStringLiteral("Settings…"), kMenuIconSettings},
            {QStringLiteral("About Better Notepad"), kMenuIconAbout},
        };
        const char *svg = byLabel.value(label, nullptr);
        return svg ? QIcon(svgMarkupPixmap(QLatin1String(svg), m_sk("--text-color"), 22)) : QIcon();
    }

    // Window-button glyphs drawn as 10x10 line icons (the font glyphs were tiny and inconsistent).
    void updateTitleIcons() {
        if (!m_maximizeButton || !m_minimizeButton || !m_closeButton)
            return;
        const QColor color = m_sk("--text-color");
        const auto icon = [&](const char *body) {
            const QString svg = QStringLiteral(
                "<svg xmlns='http://www.w3.org/2000/svg' viewBox='-1 -1 12 12' fill='none' "
                "stroke='currentColor' stroke-width='1'>%1</svg>").arg(QLatin1String(body));
            return QIcon(svgMarkupPixmap(svg, color, 14));
        };
        m_minimizeButton->setIcon(icon("<path d='M0 5H10'/>"));
        m_maximizeButton->setIcon(isMaximized()
            ? icon("<path d='M2.5 2.5V0H10V7.5H7.5M0 2.5H7.5V10H0Z'/>")
            : icon("<rect x='0' y='0' width='10' height='10'/>"));
        m_closeButton->setIcon(icon("<path d='M0 0L10 10M10 0L0 10'/>"));
        for (QToolButton *button : {m_minimizeButton, m_maximizeButton, m_closeButton})
            button->setIconSize(QSize(14, 14));
    }

    void toggleMaximized() {
        const bool maximize = !isMaximized();
        maximize ? showMaximized() : showNormal();
        updateTitleIcons();
        m_maximizeButton->setToolTip(maximize ? QStringLiteral("Restore") : QStringLiteral("Maximize"));
    }

    Editor *currentEditor() const {
        return qobject_cast<Editor *>(m_tabs->currentWidget());
    }

    void createActions() {
        auto add = [this](const QString &text, const QKeySequence &shortcut,
                          auto callback) {
            auto *action = new QAction(text, this);
            if (!shortcut.isEmpty())
                action->setShortcut(shortcut);
            connect(action, &QAction::triggered, this, callback);
            addAction(action);
            return action;
        };

        m_newAction = add(QStringLiteral("New"), QKeySequence::New, [this] { newDocument(); });
        m_openAction = add(QStringLiteral("Open…"), QKeySequence::Open, [this] { openDialog(); });
        m_saveAction = add(QStringLiteral("Save"), QKeySequence::Save, [this] { saveCurrent(); });
        m_saveAsAction = add(QStringLiteral("Save As…"), QKeySequence::SaveAs, [this] { saveCurrentAs(); });
        m_printAction = add(QStringLiteral("Print…"), QKeySequence::Print, [this] { printCurrent(); });
        m_findAction = add(QStringLiteral("Find"), QKeySequence::Find, [this] { showFind(false); });
        m_replaceAction = add(QStringLiteral("Replace"), QKeySequence::Replace, [this] { showFind(true); });
        m_reopenAction = add(QStringLiteral("Reopen Closed Tab"), QKeySequence(QStringLiteral("Ctrl+Shift+T")),
                             [this] { reopenClosedTab(); });
        // F3 / Shift+F3 step through matches of the current query; with no query yet, open the panel.
        add(QStringLiteral("Find Next"), QKeySequence(Qt::Key_F3), [this] {
            m_findInput->text().isEmpty() ? showFind(false) : findNext(false);
        });
        add(QStringLiteral("Find Previous"), QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] {
            m_findInput->text().isEmpty() ? showFind(false) : findNext(true);
        });
        m_goToAction = add(QStringLiteral("Go to Line…"), QKeySequence(QStringLiteral("Ctrl+G")),
                           [this] { goToLine(); });
        add(QStringLiteral("Close Tab"), QKeySequence(QStringLiteral("Ctrl+W")),
            [this] { closeTab(m_tabs->currentIndex()); });
        // Ctrl++ needs Shift on most layouts, so Ctrl+= zooms in too.
        add(QStringLiteral("Zoom In"), QKeySequence::ZoomIn, [this] { changeZoom(1); })
            ->setShortcuts({QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))});
        add(QStringLiteral("Zoom Out"), QKeySequence::ZoomOut, [this] { changeZoom(-1); });
        add(QStringLiteral("Reset Zoom"), QKeySequence(QStringLiteral("Ctrl+0")),
            [this] { setZoom(m_defaultZoom); });
        add(QStringLiteral("Next Tab"), QKeySequence(QStringLiteral("Ctrl+Tab")),
            [this] { changeTab(1); });
        add(QStringLiteral("Previous Tab"), QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
            [this] { changeTab(-1); });
        add(QStringLiteral("Settings…"), QKeySequence(), [this] { showSettings(); });
        add(QStringLiteral("Open Skins Folder"), QKeySequence(), [this] { openSkinsFolder(); });
        add(QStringLiteral("Reveal in Explorer"), QKeySequence(),
            [this] { revealCurrentFile(); });
    }

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_printAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_replaceAction = nullptr;
    QAction *m_goToAction = nullptr;
    QAction *m_wrapAction = nullptr;
    QAction *m_lineNumbersAction = nullptr;

    void createMenus() {
        QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
        file->addAction(m_newAction);
        file->addAction(m_openAction);
        file->addAction(m_reopenAction);
        file->addSeparator();
        file->addAction(m_saveAction);
        file->addAction(m_saveAsAction);
        auto *saveAllAction = file->addAction(QStringLiteral("Save All"));
        saveAllAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Alt+S")));
        connect(saveAllAction, &QAction::triggered, this, [this] { saveAll(); });
        file->addSeparator();
        file->addAction(m_printAction);
        file->addSeparator();
        auto *quit = file->addAction(QStringLiteral("Exit"));
        quit->setShortcut(QKeySequence::Quit);
        connect(quit, &QAction::triggered, this, &QWidget::close);

        QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
        edit->addAction(m_findAction);
        edit->addAction(m_replaceAction);
        edit->addAction(m_goToAction);

        QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
        m_wrapAction = view->addAction(QStringLiteral("Word Wrap"));
        m_wrapAction->setCheckable(true);
        m_wrapAction->setChecked(m_wordWrap);
        connect(m_wrapAction, &QAction::toggled, this, [this](bool enabled) {
            m_wordWrap = enabled;
            m_settings.setValue(QStringLiteral("editor/wordWrap"), enabled);
            forEachEditor([enabled](Editor *editor) { editor->setWordWrapEnabled(enabled); });
        });
        m_lineNumbersAction = view->addAction(QStringLiteral("Line Numbers"));
        m_lineNumbersAction->setCheckable(true);
        m_lineNumbersAction->setChecked(m_lineNumbers);
        connect(m_lineNumbersAction, &QAction::toggled, this, [this](bool enabled) {
            m_lineNumbers = enabled;
            m_settings.setValue(QStringLiteral("editor/lineNumbers"), enabled);
            forEachEditor([enabled](Editor *editor) { editor->setLineNumbersVisible(enabled); });
        });
        view->addSeparator();
        auto *settings = view->addAction(QStringLiteral("Settings…"));
        connect(settings, &QAction::triggered, this, [this] { showSettings(); });

        QMenu *help = menuBar()->addMenu(QStringLiteral("&Help"));
        auto *about = help->addAction(QStringLiteral("About Better Notepad"));
        connect(about, &QAction::triggered, this, [this] { showAbout(); });
    }

    void showAbout() {
        SettingsDialog dialog(this);
        dialog.setObjectName(QStringLiteral("aboutDialog"));
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground);
        dialog.setFixedSize(400, 260);

        const Skin &k = m_sk;
        dialog.setStyleSheet(QStringLiteral(
            "#aboutDialog{background:%1;border:1px solid %2;}"
            "QLabel{color:%3;font-size:13px;font-weight:500;}"
            "QLabel#email{color:%4;}"
            "QToolButton#dlgClose{color:%3;background:transparent;border:0;font-size:14px;}"
            "QToolButton#dlgClose:hover{color:#fff;background:%5;}"
            "QToolButton#copy{background:transparent;border:0;font-size:14px;}"
            "QPushButton#github{color:%3;background:%6;border:0;border-radius:10px;"
            "font-weight:bold;font-size:13px;}"
            "QPushButton#github:hover{background:%7;}")
            .arg(css(k("--background-color")), css(k("--border-color")), css(k("--text-color")),
                 css(k("--email-color")), css(k("--btn-hover-color-close")),
                 css(k("--gitbtn-bg")), css(k("--gitbtn-hover"))));

        auto *root = new QVBoxLayout(&dialog);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto *titleRow = new QHBoxLayout;
        titleRow->setSpacing(0);
        auto *caption = new QLabel(QStringLiteral("About"), &dialog);
        caption->setAlignment(Qt::AlignCenter);
        caption->setFixedHeight(34);
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *close = new QToolButton(&dialog);
        close->setObjectName(QStringLiteral("dlgClose"));
        close->setText(QStringLiteral("✕"));
        close->setFixedSize(40, 34);
        close->setFocusPolicy(Qt::NoFocus);
        connect(close, &QToolButton::clicked, &dialog, &QDialog::accept);
        titleRow->addSpacing(40);
        titleRow->addWidget(caption, 1);
        titleRow->addWidget(close);
        root->addLayout(titleRow);

        auto *body = new QVBoxLayout;
        body->setContentsMargins(10, 0, 10, 14);
        body->setSpacing(4);
        body->setAlignment(Qt::AlignHCenter);
        auto *logo = new QLabel(&dialog);
        // 50px wide like the Tauri #logo, rendered at 2x so it stays sharp.
        QPixmap logoPixmap = QPixmap(QStringLiteral(":/blogo.png"))
                                 .scaledToWidth(100, Qt::SmoothTransformation);
        logoPixmap.setDevicePixelRatio(2);
        logo->setPixmap(logoPixmap);
        logo->setFixedSize(logoPixmap.size() / 2);
        logo->setAlignment(Qt::AlignCenter);
        body->addWidget(logo, 0, Qt::AlignHCenter);
        for (const char *line : {"Better Notepad 2.0.0", "2026", "Created by: Hudson Pear (pyrus)"}) {
            auto *label = new QLabel(QLatin1String(line), &dialog);
            label->setAlignment(Qt::AlignCenter);
            body->addWidget(label);
        }

        auto *emailRow = new QHBoxLayout;
        emailRow->setAlignment(Qt::AlignHCenter);
        emailRow->setSpacing(4);
        auto *email = new QLabel(QStringLiteral("coolnewtabpage@gmail.com"), &dialog);
        email->setObjectName(QStringLiteral("email"));
        email->setCursor(Qt::PointingHandCursor);
        auto *copy = new QToolButton(&dialog);
        copy->setObjectName(QStringLiteral("copy"));
        copy->setText(QStringLiteral("📋"));
        copy->setToolTip(QStringLiteral("Copy"));
        copy->setCursor(Qt::PointingHandCursor);
        copy->setFocusPolicy(Qt::NoFocus);
        connect(copy, &QToolButton::clicked, &dialog, [email] {
            QApplication::clipboard()->setText(email->text());
        });
        emailRow->addWidget(email);
        emailRow->addWidget(copy);
        body->addLayout(emailRow);

        auto *github = new QPushButton(QStringLiteral("  Github"), &dialog);
        github->setObjectName(QStringLiteral("github"));
        github->setFixedSize(120, 45);
        github->setCursor(Qt::PointingHandCursor);
        github->setFocusPolicy(Qt::NoFocus);
        github->setIcon(svgPixmap(icons::github, k("--text-color"), 30, "0 0 16 16"));
        github->setIconSize(QSize(30, 30));
        connect(github, &QPushButton::clicked, &dialog,
                [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/hudsonpear"))); });
        body->addSpacing(4);
        body->addWidget(github, 0, Qt::AlignHCenter);
        root->addLayout(body);

        dialog.show();
        dialog.move(geometry().center() - QPoint(dialog.width() / 2, dialog.height() / 2));
        dialog.exec();
    }

    // Floating find/replace and go-to panels, pinned top-right over the editor like the Tauri ones.
    QFrame *createPanel() {
        auto *panel = new QFrame(centralWidget());
        panel->setObjectName(QStringLiteral("floatPanel"));
        panel->hide();
        return panel;
    }

    QPushButton *createPanelButton(const QString &text, QWidget *parent, int width = 0) {
        auto *button = new QPushButton(text, parent);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        if (width)
            button->setFixedWidth(width);
        return button;
    }

    QPushButton *createPanelClose(QWidget *parent) {
        auto *close = createPanelButton(QStringLiteral("✕"), parent, 30);
        close->setObjectName(QStringLiteral("panelClose"));
        return close;
    }

    void closePanel(QWidget *panel) {
        panel->hide();
        updateFindHighlights();
        if (Editor *editor = currentEditor())
            editor->setFocus();
    }

    void createFindBar() {
        m_findBar = createPanel();
        auto *column = new QVBoxLayout(m_findBar);
        column->setContentsMargins(10, 12, 8, 10);
        column->setSpacing(6);

        auto *top = new QHBoxLayout;
        top->setSpacing(6);
        m_findInput = new QLineEdit(m_findBar);
        m_findInput->setPlaceholderText(QStringLiteral("Find..."));
        m_findInput->setFixedWidth(150);
        // Match case / whole word / regex toggles, remembered between runs.
        const auto option = [&](const QString &text, const QString &tip, const char *key) {
            auto *button = createPanelButton(text, m_findBar, 28);
            button->setObjectName(QStringLiteral("findOpt"));
            button->setCheckable(true);
            button->setToolTip(tip);
            button->setChecked(m_settings.value(QLatin1String(key), false).toBool());
            connect(button, &QPushButton::toggled, this, [this, key](bool on) {
                m_settings.setValue(QLatin1String(key), on);
                refreshFind();
            });
            return button;
        };
        m_caseBtn = option(QStringLiteral("Aa"), QStringLiteral("Match Case"), "find/matchCase");
        m_wordBtn = option(QStringLiteral("ab"), QStringLiteral("Match Whole Word"), "find/wholeWord");
        m_regexBtn = option(QStringLiteral(".*"), QStringLiteral("Use Regular Expression"), "find/regex");
        auto *previous = createPanelButton(QStringLiteral("↑"), m_findBar, 28);
        auto *next = createPanelButton(QStringLiteral("↓"), m_findBar, 28);
        m_findResult = new QLabel(m_findBar);
        m_findResult->setMinimumWidth(66);
        m_findResult->setAlignment(Qt::AlignCenter);
        auto *close = createPanelClose(m_findBar);
        top->addWidget(m_findInput);
        top->addWidget(m_caseBtn);
        top->addWidget(m_wordBtn);
        top->addWidget(m_regexBtn);
        top->addWidget(previous);
        top->addWidget(next);
        top->addWidget(m_findResult, 1);
        top->addWidget(close);
        column->addLayout(top);

        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(6);
        m_replaceInput = new QLineEdit(m_findBar);
        m_replaceInput->setPlaceholderText(QStringLiteral("Replace..."));
        m_replaceInput->setFixedWidth(150);
        auto *replaceOneButton = createPanelButton(QStringLiteral("Replace"), m_findBar);
        auto *replaceAllButton = createPanelButton(QStringLiteral("Replace All"), m_findBar);
        bottom->addWidget(m_replaceInput);
        bottom->addWidget(replaceOneButton);
        bottom->addWidget(replaceAllButton);
        bottom->addStretch(1);
        column->addLayout(bottom);

        connect(m_findInput, &QLineEdit::textChanged, this, [this] {
            if (Editor *editor = currentEditor()) {
                // Keep extending the current match while typing instead of skipping past it.
                QTextCursor cursor = editor->textCursor();
                cursor.setPosition(cursor.selectionStart());
                editor->setTextCursor(cursor);
            }
            refreshFind();
        });
        connect(previous, &QPushButton::clicked, this, [this] { findNext(true); });
        connect(next, &QPushButton::clicked, this, [this] { findNext(false); });
        connect(m_findInput, &QLineEdit::returnPressed, this, [this] { findNext(false, false); });
        connect(m_replaceInput, &QLineEdit::returnPressed, this, [this] { replaceOne(); });
        connect(replaceOneButton, &QPushButton::clicked, this, [this] { replaceOne(); });
        connect(replaceAllButton, &QPushButton::clicked, this, [this] { replaceAll(); });
        connect(close, &QPushButton::clicked, this, [this] { closePanel(m_findBar); });
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_findBar);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        connect(escape, &QShortcut::activated, this, [this] { closePanel(m_findBar); });
    }

    void createGoToBar() {
        m_gotoBar = createPanel();
        auto *row = new QHBoxLayout(m_gotoBar);
        row->setContentsMargins(10, 12, 10, 10);
        row->setSpacing(6);
        m_gotoInput = new QLineEdit(m_gotoBar);
        m_gotoInput->setPlaceholderText(QStringLiteral("Go to line..."));
        m_gotoInput->setFixedWidth(140);
        auto *go = createPanelButton(QStringLiteral("Go"), m_gotoBar);
        auto *close = createPanelClose(m_gotoBar);
        row->addWidget(m_gotoInput);
        row->addWidget(go);
        row->addWidget(close);

        // "12" or "12:5" (line:column), jumping as you type like the Tauri panel.
        connect(m_gotoInput, &QLineEdit::textChanged, this, [this] { jumpToLine(false); });
        connect(m_gotoInput, &QLineEdit::returnPressed, this, [this] { jumpToLine(true); });
        connect(go, &QPushButton::clicked, this, [this] { jumpToLine(true); });
        connect(close, &QPushButton::clicked, this, [this] { closePanel(m_gotoBar); });
        auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), m_gotoBar);
        escape->setContext(Qt::WidgetWithChildrenShortcut);
        connect(escape, &QShortcut::activated, this, [this] { closePanel(m_gotoBar); });
    }

    void placePanels() {
        for (QFrame *panel : {m_findBar, m_gotoBar}) {
            if (!panel || !panel->isVisible())
                continue;
            panel->adjustSize();
            panel->move(centralWidget()->width() - panel->width() - 18, 35);
            panel->raise();
        }
    }

    QFrame *m_findBar = nullptr;
    QFrame *m_gotoBar = nullptr;
    QLineEdit *m_gotoInput = nullptr;
    QPushButton *m_caseBtn = nullptr;
    QPushButton *m_wordBtn = nullptr;
    QPushButton *m_regexBtn = nullptr;
    Skin m_sk;


    void applyStatusItems() {
        m_statusPath->setVisible(m_status.path);
        m_statusCursor->setVisible(m_status.cursor);
        m_statusChars->setVisible(m_status.chars);
        m_statusWords->setVisible(m_status.words);
        m_statusZoom->setVisible(m_status.zoom);
        m_zoomReset->setVisible(m_status.zoom);
        m_statusLines->setVisible(m_status.lines);
    }

    void applySettings() {
        m_wordWrap = m_settings.value(QStringLiteral("editor/wordWrap"), false).toBool();
        m_lineNumbers = m_settings.value(QStringLiteral("editor/lineNumbers"), true).toBool();
        m_skin = m_settings.value(QStringLiteral("appearance/skin"), QStringLiteral("dark")).toString();
        m_selectResults = m_settings.value(QStringLiteral("editor/selectResults"), true).toBool();
        m_restoreSession = m_settings.value(QStringLiteral("session/restore"), false).toBool();
        m_defaultZoom = m_settings.value(QStringLiteral("editor/defaultZoom"), 100).toInt();
        m_zoomStep = m_settings.value(QStringLiteral("editor/zoomStep"), 10).toInt();
        m_tabSpaces = m_settings.value(QStringLiteral("editor/tabSpaces"), 0).toInt();
        const auto flag = [&](const char *key) { return m_settings.value(QLatin1String(key), true).toBool(); };
        m_status = {flag("status/path"), flag("status/cursor"), flag("status/chars"),
                    flag("status/words"), flag("status/zoom"), flag("status/lines")};
        applyStatusItems();
        QFont defaultFont(QStringLiteral("Consolas"));
        defaultFont.setPointSizeF(10.5); // 14px, like the Tauri default
        m_baseFont = m_settings.value(QStringLiteral("editor/font"), defaultFont).value<QFont>();
        if (m_baseFont.family().isEmpty())
            m_baseFont = defaultFont;
        const QSignalBlocker wrapBlocker(m_wrapAction);
        const QSignalBlocker lineNumbersBlocker(m_lineNumbersAction);
        m_wrapAction->setChecked(m_wordWrap);
        m_lineNumbersAction->setChecked(m_lineNumbers);
        applyTheme();
    }

    void applyTheme() {
        m_sk = Skin::load(m_skin, QDir(appDataDirectory()).filePath(QStringLiteral("skins")));
        const Skin &k = m_sk;
        const QColor text = k("--text-color");
        const QColor editorBg = k("--editor-bg");

        QPalette palette;
        palette.setColor(QPalette::Window, k("--light-bg"));
        palette.setColor(QPalette::WindowText, text);
        palette.setColor(QPalette::Base, editorBg);
        palette.setColor(QPalette::AlternateBase, QColor(
            (editorBg.red() * 15 + text.red()) / 16, (editorBg.green() * 15 + text.green()) / 16,
            (editorBg.blue() * 15 + text.blue()) / 16));
        palette.setColor(QPalette::Text, text);
        palette.setColor(QPalette::Button, k("--background-color"));
        palette.setColor(QPalette::ButtonText, text);
        palette.setColor(QPalette::ToolTipBase, k("--menu-bg"));
        palette.setColor(QPalette::ToolTipText, text);
        palette.setColor(QPalette::PlaceholderText, k("--tab-font-color-inactive"));
        palette.setColor(QPalette::Highlight, editorBg.lightness() < 128 ? QColor(75, 110, 160)
                                                                          : QColor(65, 130, 210));
        palette.setColor(QPalette::HighlightedText, Qt::white);

        QString sheet = QStringLiteral(
            "QTabBar::tab{height:30px;min-width:100px;max-width:180px;padding:0 8px;margin:0 1px 0 0;"
            "border:0;border-top:4px solid transparent;border-radius:4px;"
            "background:@tabBg@;color:@tabInactive@;}"
            "QTabBar::tab:selected{border-top:4px solid @tabActive@;color:@tabActiveText@;font-weight:bold;}"
            "QTabBar::tab:hover:!selected{background:@tabHover@;}"
            "QToolButton#tabClose{color:@tabClose@;background:transparent;border:0;padding:0;font-size:15px;}"
            "QToolButton#tabClose:hover{color:@text@;background:@tabBtnHover@;}"
            "QToolButton#titleBtn,QToolButton#titleClose{color:@text@;background:transparent;border:0;}"
            "QToolButton#titleBtn:hover{background:@btnHover@;}"
            "QToolButton#titleClose:hover{background:@btnHoverClose@;color:#fff;}"
            "QStatusBar{background:@lightBg@;color:@status@;font-size:12px;border-top:1px solid @border@;}"
            "QStatusBar QLabel{color:@status@;padding:0 6px 4px 6px;}" // bottom padding lifts text 1px
            "QToolButton#statusBtn{color:@status@;background:transparent;border:0;font-size:12px;padding:0 6px 4px 6px;}"
            "QToolButton#statusBtn:hover{color:@text@;}"
            "QMenu{background:@menuBg@;color:@text@;border:1px solid @menuBorder@;border-radius:6px;"
            "padding:5px;font-size:14px;}"
            "QMenu::item{padding:7px 26px 7px 10px;border-radius:4px;}"
            "QMenu::item:selected{background:@btnHover@;}"
            "QMenu::icon{padding-left:6px;}"
            "QMenu::right-arrow{width:10px;height:10px;}"
            "QMenu::separator{height:1px;background:@menuDivider@;margin:4px 0;}"
            "QScrollBar{background:@scrollBg@;}"
            "QScrollBar:vertical{width:15px;margin:0;background:transparent;}"
            "QScrollBar:horizontal{height:15px;margin:0;}"
            "QScrollBar::handle{background:@thumb@;border-radius:5px;min-height:24px;min-width:24px;}"
            "QScrollBar::handle:vertical{background:@thumbSeeThrough@;}"
            "QScrollBar::handle:hover{background:@thumbHover@;}"
            "QScrollBar::handle:vertical:hover{background:@thumbHoverSeeThrough@;}"
            "QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}"
            "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}"
            "QAbstractScrollArea::corner{background:@corner@;}"
            "QFrame#floatPanel{background:@bg@;border:1px solid @border@;border-radius:6px;}"
            "QFrame#floatPanel QLineEdit{background:@bg@;color:@text@;border:1px solid @border@;padding:4px;"
            "selection-background-color:@highlight@;}"
            "QFrame#floatPanel QPushButton{color:@text@;background:@bg@;border:1px solid @border@;"
            "border-radius:5px;padding:3px 10px;min-height:20px;}"
            "QFrame#floatPanel QPushButton:hover{background:@btnHover@;}"
            "QFrame#floatPanel QPushButton#findOpt{padding:3px 0;}"
            "QFrame#floatPanel QPushButton#findOpt:checked{background:@btnHover@;border:1px solid @text@;}"
            "QFrame#floatPanel QLineEdit[invalid=\"true\"]{border:1px solid #c42b1c;}"
            "QFrame#floatPanel QPushButton#panelClose:hover{background:@btnHoverClose@;}"
            "QFrame#floatPanel QLabel{color:@text@;font-size:11px;}");
        const QList<QPair<QString, QColor>> tokens = {
            {QStringLiteral("@text@"), text},
            {QStringLiteral("@tabBg@"), k("--tab-bg")},
            {QStringLiteral("@tabInactive@"), k("--tab-font-color-inactive")},
            {QStringLiteral("@tabActive@"), k("--tab-active")},
            {QStringLiteral("@tabActiveText@"), k("--tab-font-color-active")},
            {QStringLiteral("@tabHover@"), k("--tab-hover")},
            {QStringLiteral("@tabClose@"), k("--tab-close-btn")},
            {QStringLiteral("@tabBtnHover@"), k("--tab-btn-hover")},
            {QStringLiteral("@btnHover@"), k("--btn-hover-color")},
            {QStringLiteral("@btnHoverClose@"), k("--btn-hover-color-close")},
            {QStringLiteral("@lightBg@"), k("--light-bg")},
            {QStringLiteral("@status@"), k("--status-btn")},
            {QStringLiteral("@border@"), k("--border-color")},
            {QStringLiteral("@menuBg@"), k("--menu-bg")},
            {QStringLiteral("@menuBorder@"), k("--menu-border")},
            {QStringLiteral("@menuDivider@"), k("--menu-divider")},
            {QStringLiteral("@scrollBg@"), k("--scroll-bg")},
            {QStringLiteral("@thumb@"), k("--scrollbar-thumb-bg")},
            {QStringLiteral("@thumbHover@"), k("--scroll-thumb-hover-bg")},
            {QStringLiteral("@thumbSeeThrough@"), withAlpha(k("--scrollbar-thumb-bg"), 150)},
            {QStringLiteral("@thumbHoverSeeThrough@"), withAlpha(k("--scroll-thumb-hover-bg"), 190)},
            {QStringLiteral("@corner@"), k("--scroll-corner")},
            {QStringLiteral("@bg@"), k("--background-color")},
            {QStringLiteral("@highlight@"), palette.color(QPalette::Highlight)},
        };
        for (const auto &token : tokens)
            sheet.replace(token.first, css(token.second));
        QApplication::setPalette(palette);
        setStyleSheet(sheet);
        updateTitleIcons();
        forEachEditor([this](Editor *editor) {
            editor->setEditorFont(m_baseFont);
            editor->setLineNumbersVisible(m_lineNumbers);
            editor->setWordWrapEnabled(m_wordWrap);
            editor->setGutterColors(m_sk("--number-bg"), m_sk("--number-color"), m_sk("--number-border"));
            editor->setTrackColor(m_sk("--scroll-bg"));
        });
        updateFindHighlights();
    }

    template <typename Callback>
    void forEachEditor(Callback callback) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *editor = qobject_cast<Editor *>(m_tabs->widget(i)))
                callback(editor);
        }
    }

    void newDocument() {
        auto *editor = createEditor();
        addEditor(editor, QStringLiteral("Untitled.txt"));
        editor->setFocus();
    }

    Editor *createEditor() {
        auto *editor = new Editor(m_tabs);
        editor->wordCount = 0;
        editor->setZoomPercent(m_defaultZoom);
        editor->setTabSpaces(m_tabSpaces);
        editor->setEditorFont(m_baseFont);
        editor->setWordWrapEnabled(m_wordWrap);
        editor->setLineNumbersVisible(m_lineNumbers);
        editor->setGutterColors(m_sk("--number-bg"), m_sk("--number-color"), m_sk("--number-border"));
        editor->setTrackColor(m_sk("--scroll-bg"));
        connect(editor, &Editor::filesDropped, this, [this](const QStringList &paths) { openFiles(paths); });
        connect(editor, &Editor::zoomWheel, this, [this, editor](int delta) {
            applyZoom(editor, editor->zoomPercent + delta * m_zoomStep);
        });
        connect(editor, &QPlainTextEdit::textChanged, this, [this, editor] {
            if (!editor->path.isEmpty() || editor->document()->isModified())
                editor->dirty = editor->document()->isModified();
            editor->wordCount = -1;
            updateTabTitle(editor);
            scheduleSessionSave();
            if (editor == currentEditor()) {
                if (m_findBar && !m_findBar->isHidden())
                    m_findHighlightTimer.start(200); // debounce: re-scan the file after typing pauses
                m_statusWords->setText(QStringLiteral("Words: …"));
                updateStatus();
                scheduleWordCount(editor);
            }
        });
        connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this, editor] {
            if (editor == currentEditor())
                updateStatus();
        });
        connect(editor, &QPlainTextEdit::selectionChanged, this, [this, editor] {
            if (editor == currentEditor())
                updateStatus();
        });
        return editor;
    }

    void addEditor(Editor *editor, const QString &label) {
        const int index = m_tabs->addTab(editor, label);
        auto *tabClose = new QToolButton(m_tabs->tabBar());
        tabClose->setText(QStringLiteral("×"));
        tabClose->setToolTip(QStringLiteral("Close tab"));
        tabClose->setAutoRaise(true);
        tabClose->setFixedSize(20, 20);
        tabClose->setObjectName(QStringLiteral("tabClose"));
        connect(tabClose, &QToolButton::clicked, this, [this, editor] {
            const int tabIndex = m_tabs->indexOf(editor);
            if (tabIndex >= 0)
                closeTab(tabIndex);
        });
        m_tabs->tabBar()->setTabButton(index, QTabBar::RightSide, tabClose);
        scheduleSessionSave();
        m_tabs->setCurrentIndex(index);
        fitTabBar();
        updateTabTitle(editor);
        updateStatus();
    }

    // The layout gives the tab bar a few pixels more than its tabs need (it reserves room for scroll
    // buttons), leaving a gap before the + button. Cap its width at the tabs' real size; it still
    // shrinks and scrolls when tabs overflow.
    void fitTabBar() {
        QTabBar *bar = m_tabs->tabBar();
        bar->setMaximumWidth(QWIDGETSIZE_MAX);
        QTimer::singleShot(0, bar, [bar] { bar->setMaximumWidth(bar->sizeHint().width()); });
    }

    void updateTabTitle(Editor *editor) {
        const int index = m_tabs->indexOf(editor);
        if (index < 0) return;
        const QString label = (editor->dirty ? QStringLiteral("*") : QString()) +
                              quotedName(editor->path);
        m_tabs->setTabText(index, label);
        fitTabBar();
        m_tabs->setTabToolTip(index, editor->path.isEmpty() ? label : editor->path);
        if (editor == currentEditor())
            setWindowTitle(label + QStringLiteral(" — Better Notepad"));
    }

    void updateStatus() {
        Editor *editor = currentEditor();
        if (!editor) return;
        const QTextCursor cursor = editor->textCursor();
        m_statusPath->setText(editor->path.isEmpty()
            ? QStringLiteral("Untitled (not saved)")
            : QDir::toNativeSeparators(editor->path));
        m_statusCursor->setText(QStringLiteral("Line %1, Col %2")
            .arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
        m_statusLines->setText(QStringLiteral("Lines: %1").arg(editor->document()->blockCount()));
        const int chars = cursor.hasSelection() ? cursor.selectionEnd() - cursor.selectionStart()
                                                 : editor->document()->characterCount() - 1;
        m_statusChars->setText(QStringLiteral("Chars: %1").arg(chars));
        m_statusWords->setText(editor->wordCount < 0
            ? QStringLiteral("Words: …")
            : QStringLiteral("Words: %1").arg(editor->wordCount));
        m_statusZoom->setText(QStringLiteral("%1%").arg(editor->zoomPercent));
    }

    void scheduleWordCount(Editor *editor) {
        const quint64 generation = ++editor->wordCountGeneration;
        QTimer::singleShot(450, editor, [this, editor, generation] {
            if (generation != editor->wordCountGeneration || editor != currentEditor())
                return;
            const QString snapshot = editor->toPlainText();
            const QPointer<Editor> guardedEditor(editor);
            auto *watcher = new QFutureWatcher<qsizetype>(this);
            m_wordWatchers.insert(editor, watcher);
            connect(watcher, &QFutureWatcher<qsizetype>::finished, this, [this, editor, guardedEditor, watcher, generation] {
                const qsizetype words = watcher->result();
                watcher->deleteLater();
                if (m_wordWatchers.value(editor) == watcher)
                    m_wordWatchers.remove(editor);
                if (guardedEditor && guardedEditor == currentEditor() &&
                    generation == guardedEditor->wordCountGeneration) {
                    guardedEditor->wordCount = words;
                    m_statusWords->setText(QStringLiteral("Words: %1").arg(words));
                }
            });
            watcher->setFuture(QtConcurrent::run([snapshot] { return countWords(snapshot); }));
        });
    }

    void openDialog() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this, QStringLiteral("Open files"), {},
            QStringLiteral("Text files (*.txt *.md *.log *.ini *.json *.cfg *.js *.html *.htm *.css *.vbs *.reg *.xml *.sh *.ps1);;All files (*)"));
        openFiles(files);
    }

    void openFiles(const QStringList &files) {
        for (const QString &path : files) {
            if (path.isEmpty()) continue;
            Editor *alreadyOpen = nullptr;
            forEachEditor([&](Editor *editor) {
                if (QFileInfo(editor->path) == QFileInfo(path))
                    alreadyOpen = editor;
            });
            if (alreadyOpen) {
                m_tabs->setCurrentWidget(alreadyOpen);
                continue;
            }
            m_openQueue.append(path);
        }
        loadNextFile();
    }

    void loadNextFile() {
        if (m_loadingEditor || m_openQueue.isEmpty()) return;
        const QString path = m_openQueue.takeFirst();
        auto *editor = createEditor();
        editor->setReadOnly(true);
        editor->path = path;
        editor->setPlaceholderText(QStringLiteral("Loading…"));
        addEditor(editor, QStringLiteral("Loading…"));
        m_loadingEditor = editor;

        auto *watcher = new QFutureWatcher<LoadedFile>(this);
        m_loadWatchers.insert(editor, watcher);
        connect(watcher, &QFutureWatcher<LoadedFile>::finished, this, [this, editor, watcher] {
            const LoadedFile result = watcher->result();
            watcher->deleteLater();
            m_loadWatchers.remove(editor);
            m_loadingEditor = nullptr;
            if (!result.error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Open failed"),
                    QStringLiteral("Could not open %1:\n%2").arg(result.path, result.error));
                const int index = m_tabs->indexOf(editor);
                if (index >= 0) m_tabs->removeTab(index);
                editor->deleteLater();
            } else {
                editor->setReadOnly(false);
                editor->setPlainText(result.text);
                editor->document()->setModified(false);
                editor->dirty = false;
                const int savedZoom = m_settings.value(zoomKey(editor->path), m_defaultZoom).toInt();
                if (savedZoom != editor->zoomPercent)
                    applyZoom(editor, savedZoom);
                updateTabTitle(editor);
                scheduleWordCount(editor);
                updateStatus();
            }
            scheduleSessionSave();
            loadNextFile();
        });
        watcher->setFuture(QtConcurrent::run([path] { return readFile(path); }));
    }

    bool saveEditor(Editor *editor, bool forceDialog = false) {
        if (!editor) return false;
        QString path = editor->path;
        if (path.isEmpty() || forceDialog) {
            path = QFileDialog::getSaveFileName(this, QStringLiteral("Save file"),
                                                path.isEmpty() ? QStringLiteral("Untitled.txt") : path,
                                                QStringLiteral("Text files (*.txt);;All files (*)"));
            if (path.isEmpty()) return false;
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, QStringLiteral("Save failed"), file.errorString());
            return false;
        }
        const QByteArray bytes = editor->toPlainText().toUtf8();
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::critical(this, QStringLiteral("Save failed"), file.errorString());
            return false;
        }
        editor->path = path;
        applyZoom(editor, editor->zoomPercent); // store zoom under the new path
        editor->dirty = false;
        editor->document()->setModified(false);
        updateTabTitle(editor);
        updateStatus();
        scheduleSessionSave();
        return true;
    }

    void saveCurrent() {
        saveEditor(currentEditor());
    }

    void saveCurrentAs() {
        saveEditor(currentEditor(), true);
    }

    bool saveAll() {
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
            if (editor && editor->dirty && !saveEditor(editor))
                return false;
        }
        return true;
    }

    void closeTab(int index) {
        if (index < 0) return;
        auto *editor = qobject_cast<Editor *>(m_tabs->widget(index));
        if (!editor) return;
        if (editor->dirty) {
            QMessageBox box(QMessageBox::Warning, QStringLiteral("Unsaved changes"),
                            QStringLiteral("Save changes to %1?").arg(quotedName(editor->path)),
                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
            box.setDefaultButton(QMessageBox::Save);
            const auto result = static_cast<QMessageBox::StandardButton>(box.exec());
            if (result == QMessageBox::Cancel) return;
            if (result == QMessageBox::Save && !saveEditor(editor)) return;
        }
        rememberClosedTab(editor);
        auto *watcher = m_loadWatchers.take(editor);
        if (watcher) {
            watcher->disconnect(this);
            watcher->deleteLater();
            if (m_loadingEditor == editor) m_loadingEditor = nullptr;
        }
        m_tabs->removeTab(index);
        editor->deleteLater();
        fitTabBar();
        scheduleSessionSave();
        if (m_tabs->count() == 0)
            newDocument();
        else
            updateStatus();
        loadNextFile();
    }

    // ---- Session: unsaved tabs are backed up to <appdata>/session so a crash or kill loses nothing.
    QString sessionDir() const { return QDir(appDataDirectory()).filePath(QStringLiteral("session")); }

    void scheduleSessionSave() {
        if (m_restoreSession)
            m_sessionTimer.start(1500);
    }

    // Writes session.json (open tabs, cursor, zoom) plus one <id>.bak per tab with unsaved text.
    // discardDirty: the user threw their unsaved changes away, so only clean file tabs are kept.
    // ponytail: writes backups on the UI thread; move to a worker if multi-MB unsaved files stutter.
    void saveSession(bool discardDirty = false) {
        const QString dir = sessionDir();
        if (!m_restoreSession) {
            QDir(dir).removeRecursively();
            return;
        }
        QDir().mkpath(dir);
        QJsonArray tabs;
        QSet<QString> keep;
        int current = 0;
        for (int i = 0; i < m_tabs->count(); ++i) {
            auto *editor = qobject_cast<Editor *>(m_tabs->widget(i));
            if (!editor)
                continue;
            const bool loading = m_loadWatchers.contains(editor);
            const bool unsaved = editor->dirty || (editor->path.isEmpty() && !editor->document()->isEmpty());
            const bool backup = !discardDirty && !loading && unsaved;
            if (editor->path.isEmpty() && !backup)
                continue;
            if (i == m_tabs->currentIndex())
                current = static_cast<int>(tabs.size());
            if (backup) {
                keep.insert(editor->sessionId);
                if (editor->backupRevision != editor->document()->revision()) {
                    QSaveFile file(QDir(dir).filePath(editor->sessionId + QStringLiteral(".bak")));
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(editor->toPlainText().toUtf8());
                        if (file.commit())
                            editor->backupRevision = editor->document()->revision();
                    }
                }
            }
            QJsonObject tab;
            tab[QStringLiteral("id")] = editor->sessionId;
            tab[QStringLiteral("path")] = editor->path;
            tab[QStringLiteral("backup")] = backup;
            tab[QStringLiteral("zoom")] = editor->zoomPercent;
            tab[QStringLiteral("cursor")] = editor->textCursor().position();
            tabs.append(tab);
        }
        QJsonObject root;
        root[QStringLiteral("tabs")] = tabs;
        root[QStringLiteral("current")] = current;
        QSaveFile file(QDir(dir).filePath(QStringLiteral("session.json")));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
            file.commit();
        }
        // Drop backups of tabs that were saved or closed since.
        for (const QString &name : QDir(dir).entryList({QStringLiteral("*.bak")}, QDir::Files)) {
            if (!keep.contains(QFileInfo(name).completeBaseName()))
                QFile::remove(QDir(dir).filePath(name));
        }
    }

    // Reopens the tabs of the last session. Files are read synchronously (they were open a moment ago).
    bool restoreSession() {
        if (!m_restoreSession)
            return false;
        QFile file(QDir(sessionDir()).filePath(QStringLiteral("session.json")));
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        int restored = 0;
        int current = -1;
        const QJsonArray tabs = root.value(QStringLiteral("tabs")).toArray();
        for (int i = 0; i < tabs.size(); ++i) {
            const QJsonObject tab = tabs[i].toObject();
            const QString path = tab.value(QStringLiteral("path")).toString();
            const QString backupPath = QDir(sessionDir()).filePath(
                tab.value(QStringLiteral("id")).toString() + QStringLiteral(".bak"));
            LoadedFile source;
            bool dirty = false;
            if (tab.value(QStringLiteral("backup")).toBool()) {
                source = readFile(backupPath);
                dirty = true;
            } else if (!path.isEmpty()) {
                source = readFile(path);
            } else {
                continue;
            }
            if (!source.error.isEmpty())
                continue; // the file (or its backup) is gone
            createRestoredEditor(path, source.text, dirty, tab.value(QStringLiteral("zoom")).toInt(100),
                                 tab.value(QStringLiteral("cursor")).toInt());
            if (i == root.value(QStringLiteral("current")).toInt())
                current = m_tabs->count() - 1;
            ++restored;
        }
        if (restored > 0) {
            m_tabs->setCurrentIndex(current >= 0 ? current : m_tabs->count() - 1);
            if (Editor *editor = currentEditor()) {
                scheduleWordCount(editor);
                editor->setFocus();
            }
            updateStatus();
        }
        return restored > 0;
    }

    Editor *createRestoredEditor(const QString &path, const QString &text, bool dirty, int zoom, int cursorPosition) {
        Editor *editor = createEditor();
        editor->path = path;
        editor->setPlainText(text);
        editor->document()->setModified(dirty);
        editor->dirty = dirty;
        addEditor(editor, quotedName(path));
        if (zoom != editor->zoomPercent)
            applyZoom(editor, zoom);
        QTextCursor cursor(editor->document());
        cursor.setPosition(qBound(0, cursorPosition, editor->document()->characterCount() - 1));
        editor->setTextCursor(cursor);
        updateTabTitle(editor);
        return editor;
    }

    // ---- Ctrl+Shift+T: closed tabs come back with their unsaved text, cursor and zoom.
    void rememberClosedTab(Editor *editor) {
        if (m_loadWatchers.contains(editor))
            return; // still loading, nothing worth restoring
        const bool unsaved = editor->dirty || editor->path.isEmpty();
        if (editor->path.isEmpty() && editor->document()->isEmpty())
            return;
        ClosedTab tab;
        tab.path = editor->path;
        tab.hasText = unsaved;
        tab.text = unsaved ? editor->toPlainText() : QString();
        tab.dirty = editor->dirty || editor->path.isEmpty();
        tab.zoom = editor->zoomPercent;
        tab.cursor = editor->textCursor().position();
        m_closedTabs.append(tab);
        while (m_closedTabs.size() > 20)
            m_closedTabs.removeFirst();
    }

    void reopenClosedTab() {
        if (m_closedTabs.isEmpty())
            return;
        const ClosedTab tab = m_closedTabs.takeLast();
        if (!tab.hasText) {
            openFiles({tab.path}); // unchanged file: reload from disk (or just focus it if it is open)
            return;
        }
        createRestoredEditor(tab.path, tab.text, tab.dirty, tab.zoom, tab.cursor)->setFocus();
    }

    void printCurrent() {
        Editor *editor = currentEditor();
        if (!editor) return;
        QPrinter printer(QPrinter::HighResolution);
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() == QDialog::Accepted)
            editor->print(&printer);
    }

    void showFind(bool replace) {
        m_gotoBar->hide();
        Editor *editor = currentEditor();
        if (editor && editor->textCursor().hasSelection() &&
            !editor->textCursor().selectedText().contains(QChar::ParagraphSeparator))
            m_findInput->setText(editor->textCursor().selectedText());
        m_findBar->show();
        placePanels();
        QLineEdit *target = replace ? m_replaceInput : m_findInput;
        target->setFocus();
        target->selectAll();
        updateFindCount();
        updateFindHighlights();
    }

    // The find box + the three option toggles as one regex (literal text is escaped).
    QRegularExpression findRegex() const {
        const QString text = m_findInput->text();
        if (text.isEmpty())
            return {};
        QString pattern = m_regexBtn->isChecked() ? text : QRegularExpression::escape(text);
        if (m_wordBtn->isChecked())
            pattern = QStringLiteral("\\b(?:%1)\\b").arg(pattern);
        QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
        if (!m_caseBtn->isChecked())
            options |= QRegularExpression::CaseInsensitiveOption;
        return QRegularExpression(pattern, options);
    }

    // Query/options changed: recount, re-highlight and jump to the next match.
    void refreshFind() {
        updateFindCount();
        updateFindHighlights();
        findNext(false, false);
    }

    // "Select Results on Find": highlight all matches + scrollbar map, only while the find panel is open.
    void updateFindHighlights() {
        const bool on = m_selectResults && m_findBar && !m_findBar->isHidden();
        const QRegularExpression regex = on ? findRegex() : QRegularExpression();
        const QColor color = m_sk("--highlightmark-color");
        Editor *current = currentEditor();
        forEachEditor([&](Editor *editor) {
            editor->setFindHighlight(editor == current ? regex : QRegularExpression(), color);
        });
    }

    // "3 of 28" (or "? of 28" when no match is selected), "No results", or "Invalid" for a bad regex.
    void updateFindCount() {
        Editor *editor = currentEditor();
        const QRegularExpression regex = findRegex();
        const bool invalid = !regex.pattern().isEmpty() && !regex.isValid();
        if (m_findInput->property("invalid").toBool() != invalid) {
            m_findInput->setProperty("invalid", invalid);
            m_findInput->style()->unpolish(m_findInput);
            m_findInput->style()->polish(m_findInput);
        }
        if (!editor || regex.pattern().isEmpty()) {
            m_findResult->clear();
            return;
        }
        if (invalid) {
            m_findResult->setText(QStringLiteral("Invalid"));
            return;
        }
        // ponytail: rescans the whole document per keystroke; cache if huge files feel slow.
        const QTextCursor cursor = editor->textCursor();
        int total = 0;
        int current = 0;
        QRegularExpressionMatchIterator it = regex.globalMatch(editor->toPlainText());
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (match.capturedLength() == 0)
                continue;
            ++total;
            if (cursor.hasSelection() && match.capturedStart() == cursor.selectionStart() &&
                match.capturedEnd() == cursor.selectionEnd())
                current = total;
        }
        m_findResult->setText(total == 0 ? QStringLiteral("No results")
                                         : QStringLiteral("%1 of %2")
                                               .arg(current ? QString::number(current) : QStringLiteral("?"))
                                               .arg(total));
    }

    QTextDocument::FindFlags findFlags(bool backwards = false) const {
        QTextDocument::FindFlags flags;
        if (backwards) flags |= QTextDocument::FindBackward;
        return flags;
    }

    void findNext(bool backwards, bool focusEditor = true) {
        Editor *editor = currentEditor();
        const QRegularExpression regex = findRegex();
        if (!editor || regex.pattern().isEmpty() || !regex.isValid()) return;
        if (!editor->find(regex, findFlags(backwards))) {
            QTextCursor cursor = editor->textCursor();
            cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
            editor->setTextCursor(cursor);
            editor->find(regex, findFlags(backwards));
        }
        updateFindCount();
        if (focusEditor)
            editor->setFocus();
    }

    // "$1".."$9" in the replacement text expand to capture groups, only in regex mode.
    QString replacementFor(const QRegularExpressionMatch &match) const {
        const QString text = m_replaceInput->text();
        if (!m_regexBtn->isChecked())
            return text;
        QString out;
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text[i] == QLatin1Char('$') && i + 1 < text.size() && text[i + 1].isDigit()) {
                out += match.captured(text[i + 1].digitValue());
                ++i;
            } else {
                out += text[i];
            }
        }
        return out;
    }

    void replaceOne() {
        Editor *editor = currentEditor();
        const QRegularExpression regex = findRegex();
        if (!editor || regex.pattern().isEmpty() || !regex.isValid()) return;
        // The selection counts only if the whole of it is one match.
        const auto matchOf = [&](const QTextCursor &c) {
            const QString selected = c.selectedText();
            const QRegularExpressionMatch m = regex.match(selected);
            return (c.hasSelection() && m.hasMatch() && m.capturedStart() == 0 &&
                    m.capturedLength() == selected.size()) ? m : QRegularExpressionMatch();
        };
        QTextCursor cursor = editor->textCursor();
        if (!matchOf(cursor).hasMatch()) {
            findNext(false, false);
            cursor = editor->textCursor();
        }
        const QRegularExpressionMatch match = matchOf(cursor);
        if (match.hasMatch())
            cursor.insertText(replacementFor(match));
        findNext(false, false);
        updateFindHighlights(); // the replaced text moved the matches
    }

    void replaceAll() {
        Editor *editor = currentEditor();
        if (!editor || m_findInput->text().isEmpty()) return;
        const QRegularExpression regex = findRegex();
        if (!regex.isValid()) return;
        QTextCursor cursor(editor->document());
        cursor.beginEditBlock();
        qsizetype replaced = 0;
        while (true) {
            cursor = editor->document()->find(regex, cursor);
            if (cursor.isNull()) break;
            if (!cursor.hasSelection()) { // empty regex match: step over it so we cannot loop forever
                if (!cursor.movePosition(QTextCursor::NextCharacter)) break;
                continue;
            }
            cursor.insertText(replacementFor(regex.match(cursor.selectedText())));
            ++replaced;
        }
        cursor.endEditBlock();
        m_findResult->setText(QStringLiteral("%1 replaced").arg(replaced));
    }

    void goToLine() {
        m_findBar->hide();
        updateFindHighlights();
        m_gotoInput->clear();
        m_gotoBar->show();
        placePanels();
        m_gotoInput->setFocus();
    }

    void jumpToLine(bool focusEditor) {
        Editor *editor = currentEditor();
        if (!editor) return;
        const QStringList parts = m_gotoInput->text().split(QLatin1Char(':'));
        const int line = parts.value(0).trimmed().toInt();
        const int column = parts.size() > 1 ? parts.value(1).trimmed().toInt() : 1;
        const QTextBlock block = editor->document()->findBlockByNumber(line - 1);
        if (line < 1 || !block.isValid()) return;
        QTextCursor cursor(block);
        cursor.setPosition(block.position() + qBound(0, column - 1, block.length() - 1));
        editor->setTextCursor(cursor);
        editor->centerCursor();
        if (focusEditor)
            editor->setFocus();
    }

    // direction is +1 / -1; the step (5% or 10%) comes from Settings.
    void changeZoom(int direction) {
        Editor *editor = currentEditor();
        if (editor) setZoom(editor->zoomPercent + direction * m_zoomStep);
    }

    void setZoom(int zoom) {
        if (Editor *editor = currentEditor())
            applyZoom(editor, zoom);
    }

    // Zoom is remembered per file (settings.ini, zoom/<base64 path>); 100% is the default and stored as nothing.
    QString zoomKey(const QString &path) const {
        return QStringLiteral("zoom/") + QString::fromLatin1(
            QDir::cleanPath(path).toLower().toUtf8().toBase64(QByteArray::Base64UrlEncoding));
    }

    void applyZoom(Editor *editor, int zoom) {
        editor->setZoomPercent(zoom);
        editor->setEditorFont(m_baseFont);
        if (!editor->path.isEmpty()) {
            if (editor->zoomPercent == m_defaultZoom)
                m_settings.remove(zoomKey(editor->path));
            else
                m_settings.setValue(zoomKey(editor->path), editor->zoomPercent);
        }
        if (editor == currentEditor())
            updateStatus();
    }

    void changeTab(int delta) {
        if (m_tabs->count() < 2) return;
        const int next = (m_tabs->currentIndex() + delta + m_tabs->count()) % m_tabs->count();
        m_tabs->setCurrentIndex(next);
    }

    void showSettings() {
        SettingsDialog dialog(this);
        dialog.setObjectName(QStringLiteral("settingsDialog"));
        dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground);
        dialog.setFixedWidth(640);

        auto *root = new QVBoxLayout(&dialog);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // Title strip: centred caption + close button.
        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(0);
        auto *caption = new QLabel(QStringLiteral("Settings"), &dialog);
        caption->setAlignment(Qt::AlignCenter);
        caption->setFixedHeight(34);
        caption->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *closeButton = new QToolButton(&dialog);
        closeButton->setObjectName(QStringLiteral("dlgClose"));
        closeButton->setText(QStringLiteral("✕"));
        closeButton->setFixedSize(40, 34);
        closeButton->setFocusPolicy(Qt::NoFocus);
        connect(closeButton, &QToolButton::clicked, &dialog, &QDialog::accept);
        titleRow->addSpacing(40);
        titleRow->addWidget(caption, 1);
        titleRow->addWidget(closeButton);
        root->addLayout(titleRow);

        // Tabs.
        auto *tabsRow = new QHBoxLayout;
        tabsRow->setContentsMargins(20, 0, 20, 0);
        tabsRow->setSpacing(6);
        auto *group = new QButtonGroup(&dialog);
        auto *generalTab = new QToolButton(&dialog);
        auto *hotkeysTab = new QToolButton(&dialog);
        for (auto *tab : {generalTab, hotkeysTab}) {
            tab->setObjectName(QStringLiteral("tab"));
            tab->setCheckable(true);
            tab->setCursor(Qt::PointingHandCursor);
            tab->setFocusPolicy(Qt::NoFocus);
            group->addButton(tab);
            tabsRow->addWidget(tab);
        }
        generalTab->setText(QStringLiteral("Settings"));
        hotkeysTab->setText(QStringLiteral("Hotkeys"));
        generalTab->setChecked(true);
        tabsRow->addStretch(1);
        root->addLayout(tabsRow);
        auto *rule = new QFrame(&dialog);
        rule->setObjectName(QStringLiteral("rule"));
        rule->setFixedHeight(1);
        root->addWidget(rule);

        auto *pages = new QStackedWidget(&dialog);
        root->addWidget(pages);
        connect(generalTab, &QToolButton::clicked, pages, [pages] { pages->setCurrentIndex(0); });
        connect(hotkeysTab, &QToolButton::clicked, pages, [pages] { pages->setCurrentIndex(1); });

        // General page: two columns of titled sections.
        auto *general = new QWidget(pages);
        auto *columns = new QHBoxLayout(general);
        columns->setContentsMargins(20, 16, 20, 20);
        columns->setSpacing(30);
        auto *left = new QVBoxLayout;
        auto *right = new QVBoxLayout;
        for (auto *column : {left, right})
            column->setSpacing(12);
        columns->addLayout(left, 1);
        columns->addLayout(right, 1);

        QList<QPair<QLabel *, const char *>> iconLabels;
        QList<ToggleSwitch *> toggles;
        const auto addSection = [&](QVBoxLayout *column, const QString &title, bool first = false) {
            if (!first)
                column->addSpacing(10);
            auto *label = new QLabel(title.toUpper(), general);
            label->setObjectName(QStringLiteral("section"));
            column->addWidget(label);
        };
        auto addRow = [&](QVBoxLayout *column, const char *iconPath, const QString &text, auto &&...controls) {
            auto *row = new QHBoxLayout;
            row->setSpacing(8);
            auto *icon = new QLabel(general);
            icon->setFixedSize(22, 22);
            iconLabels.append({icon, iconPath});
            row->addWidget(icon);
            row->addWidget(new QLabel(text, general), 1);
            (row->addWidget(controls), ...);
            column->addLayout(row);
        };
        // Wide controls go on their own line under the label, indented past the icon.
        auto addStackedRow = [&](QVBoxLayout *column, const char *iconPath, const QString &text, auto &&...controls) {
            auto *header = new QHBoxLayout;
            header->setSpacing(8);
            auto *icon = new QLabel(general);
            icon->setFixedSize(22, 22);
            iconLabels.append({icon, iconPath});
            header->addWidget(icon);
            header->addWidget(new QLabel(text, general), 1);
            column->addLayout(header);
            auto *line = new QHBoxLayout;
            line->setContentsMargins(30, 0, 0, 0);
            line->setSpacing(6);
            (line->addWidget(controls), ...);
            line->addStretch(1);
            column->addLayout(line);
        };
        auto addToggle = [&](QVBoxLayout *column, const char *iconPath, const QString &text, bool on) {
            auto *toggle = new ToggleSwitch(on, general);
            toggles.append(toggle);
            addRow(column, iconPath, text, toggle);
            return toggle;
        };
        // Combo with fixed items (text, value); selects the entry whose value equals `current`.
        const auto addCombo = [&](const QList<QPair<QString, int>> &items, int current, int width) {
            auto *combo = new QComboBox(general);
            combo->setFixedWidth(width);
            for (const auto &item : items)
                combo->addItem(item.first, item.second);
            combo->setCurrentIndex(qMax(0, combo->findData(current)));
            return combo;
        };

        // -- Left column: Appearance + Editor
        addSection(left, QStringLiteral("Appearance"), true);
        auto *skinsFolder = new QPushButton(QStringLiteral("Skins Folder"), general);
        connect(skinsFolder, &QPushButton::clicked, this, [this] { openSkinsFolder(); });
        auto *skin = new QComboBox(general);
        skin->setFixedWidth(120);
        for (const QString &name : Skin::builtInNames())
            skin->addItem(name.at(0).toUpper() + name.mid(1), name);
        const QStringList custom = Skin::customNames(QDir(appDataDirectory()).filePath(QStringLiteral("skins")));
        if (!custom.isEmpty())
            skin->insertSeparator(skin->count());
        for (const QString &name : custom)
            skin->addItem(name, name);
        skin->setCurrentIndex(qMax(0, skin->findData(m_skin)));
        addStackedRow(left, icons::skin, QStringLiteral("Skin Selector"), skin, skinsFolder);

        auto *fontFamily = new QComboBox(general);
        fontFamily->setFixedWidth(108);
        fontFamily->addItems({QStringLiteral("Consolas"), QStringLiteral("Cascadia Code"),
                              QStringLiteral("JetBrains Mono"), QStringLiteral("Courier New"),
                              QStringLiteral("Segoe UI"), QStringLiteral("Arial")});
        if (fontFamily->findText(m_baseFont.family()) < 0)
            fontFamily->addItem(m_baseFont.family());
        fontFamily->setCurrentText(m_baseFont.family());
        auto *fontSize = new QComboBox(general);
        fontSize->setFixedWidth(60);
        const int currentPx = qRound(m_baseFont.pointSizeF() / 0.75);
        for (int px : {12, 13, 14, 15, 16, 18})
            fontSize->addItem(QStringLiteral("%1px").arg(px), px);
        if (fontSize->findData(currentPx) < 0)
            fontSize->addItem(QStringLiteral("%1px").arg(currentPx), currentPx);
        fontSize->setCurrentIndex(fontSize->findData(currentPx));
        auto *fontWeight = new QComboBox(general);
        fontWeight->setFixedWidth(78);
        fontWeight->addItem(QStringLiteral("Normal"), 400);
        fontWeight->addItem(QStringLiteral("Medium"), 500);
        fontWeight->addItem(QStringLiteral("Semi-Bold"), 600);
        fontWeight->addItem(QStringLiteral("Bold"), 700);
        fontWeight->setCurrentIndex(qMax(0, fontWeight->findData(static_cast<int>(m_baseFont.weight()))));
        addStackedRow(left, icons::font, QStringLiteral("Font"), fontFamily, fontSize, fontWeight);

        addSection(left, QStringLiteral("Editor"));
        auto *wrap = addToggle(left, icons::wrap, QStringLiteral("Line Break/Word Wrap"), m_wordWrap);
        auto *numbers = addToggle(left, icons::numbers, QStringLiteral("Display Line Numbers"), m_lineNumbers);
        auto *selectResults = addToggle(left, icons::find, QStringLiteral("Select Results on Find"), m_selectResults);
        auto *tabSpaces = addCombo({{QStringLiteral("Default (Tab)"), 0}, {QStringLiteral("2 spaces"), 2},
                                    {QStringLiteral("4 spaces"), 4}, {QStringLiteral("6 spaces"), 6},
                                    {QStringLiteral("8 spaces"), 8}}, m_tabSpaces, 130);
        addRow(left, icons::tabKey, QStringLiteral("Tab Key"), tabSpaces);
        left->addStretch(1);

        // -- Right column: Zoom + Status bar + Session
        addSection(right, QStringLiteral("Zoom"), true);
        auto *defaultZoom = addCombo({{QStringLiteral("100%"), 100}, {QStringLiteral("120%"), 120},
                                      {QStringLiteral("150%"), 150}}, m_defaultZoom, 130);
        addRow(right, icons::zoom, QStringLiteral("Default Zoom"), defaultZoom);
        auto *zoomStep = addCombo({{QStringLiteral("5%"), 5}, {QStringLiteral("10%"), 10}}, m_zoomStep, 130);
        addRow(right, icons::zoom, QStringLiteral("Zoom Step"), zoomStep);

        addSection(right, QStringLiteral("Status bar"));
        auto *status = addToggle(right, icons::statusbar, QStringLiteral("Display Statusbar"), statusBar()->isVisible());
        auto *itemsGrid = new QGridLayout;
        itemsGrid->setContentsMargins(30, 0, 0, 0);
        itemsGrid->setHorizontalSpacing(16);
        itemsGrid->setVerticalSpacing(8);
        QList<TickCheckBox *> statusChecks;
        const struct { const char *label; bool on; } statusItems[] = {
            {"Path", m_status.path}, {"Line / Col", m_status.cursor}, {"Characters", m_status.chars},
            {"Words", m_status.words}, {"Zoom", m_status.zoom}, {"Lines", m_status.lines}};
        for (int i = 0; i < 6; ++i) {
            auto *check = new TickCheckBox(QLatin1String(statusItems[i].label), general);
            check->setChecked(statusItems[i].on);
            check->setEnabled(statusBar()->isVisible());
            check->setCursor(Qt::PointingHandCursor);
            statusChecks.append(check);
            itemsGrid->addWidget(check, i / 2, i % 2);
        }
        right->addLayout(itemsGrid);
        connect(status, &QAbstractButton::toggled, general, [statusChecks](bool on) {
            for (QCheckBox *check : statusChecks)
                check->setEnabled(on);
        });

        addSection(right, QStringLiteral("Session"));
        auto *restoreSession = addToggle(right, icons::history, QStringLiteral("Remember Last Session"), m_restoreSession);
        right->addStretch(1);
        pages->addWidget(general);

        // Hotkeys page: every shortcut registered on the window, in two columns.
        auto *hotkeys = new QWidget(pages);
        auto *hotkeyColumns = new QHBoxLayout(hotkeys);
        hotkeyColumns->setContentsMargins(20, 16, 20, 20);
        hotkeyColumns->setSpacing(30);
        QList<QAction *> shortcuts;
        for (QAction *action : actions()) {
            if (!action->shortcut().isEmpty())
                shortcuts << action;
        }
        for (int c = 0; c < 2; ++c) {
            auto *form = new QFormLayout;
            form->setVerticalSpacing(8);
            const int half = (static_cast<int>(shortcuts.size()) + 1) / 2;
            for (int i = c * half; i < qMin<int>(shortcuts.size(), (c + 1) * half); ++i) {
                form->addRow(shortcuts[i]->text().remove(QStringLiteral("…")),
                             new QLabel(shortcuts[i]->shortcut().toString(QKeySequence::NativeText), hotkeys));
            }
            hotkeyColumns->addLayout(form, 1);
        }
        pages->addWidget(hotkeys);
        // Keep the dialog the same height on both tabs.
        pages->setMinimumHeight(general->sizeHint().height());

        // Colours come from the active skin, so this runs again whenever the skin changes.
        auto restyle = [&] {
            const Skin &k = m_sk;
            QString sheet = QStringLiteral(
                "#settingsDialog{background:@bg@;border:1px solid @border@;}"
                "QLabel{color:@text@;font-size:13px;}"
                "QLabel#section{color:@tabInactive@;font-size:11px;font-weight:bold;letter-spacing:1px;}"
                "QComboBox{color:@text@;background:@bg@;border:2px solid @border@;border-radius:3px;"
                "padding:2px 6px;min-height:22px;}"
                "QComboBox QAbstractItemView{background:@bg@;color:@text@;border:1px solid @border@;"
                "selection-background-color:@tabActive@;selection-color:@text@;}"
                "QPushButton{color:@text@;background:@bg@;border:1px solid @border@;border-radius:5px;"
                "padding:3px 10px;min-height:22px;}"
                "QPushButton:hover{background:@btnHover@;}"
                "QCheckBox{color:@text@;font-size:13px;spacing:8px;}"
                "QCheckBox:disabled{color:@tabInactive@;}"
                "QCheckBox::indicator{width:15px;height:15px;border:1px solid @border@;border-radius:3px;"
                "background:@bg@;}"
                "QCheckBox::indicator:checked{background:@sliderOn@;border:1px solid @text@;}"
                "QToolButton#dlgClose{color:@text@;background:transparent;border:0;font-size:14px;}"
                "QToolButton#dlgClose:hover{color:#fff;background:@btnHoverClose@;}"
                "QToolButton#tab{border:0;border-bottom:2px solid transparent;padding:6px 10px;"
                "color:@tabInactive@;font-size:13px;}"
                "QToolButton#tab:checked{color:@text@;border-bottom:2px solid @text@;}"
                "QFrame#rule{background:@border@;}");
            const QList<QPair<QString, QColor>> tokens = {
                {QStringLiteral("@bg@"), k("--background-color")},
                {QStringLiteral("@border@"), k("--border-color")},
                {QStringLiteral("@text@"), k("--text-color")},
                {QStringLiteral("@tabActive@"), k("--tab-active")},
                {QStringLiteral("@btnHover@"), k("--btn-hover-color")},
                {QStringLiteral("@btnHoverClose@"), k("--btn-hover-color-close")},
                {QStringLiteral("@tabInactive@"), k("--tab-font-color-inactive")},
                {QStringLiteral("@sliderOn@"), k("--slider-bg-checked")},
            };
            for (const auto &token : tokens)
                sheet.replace(token.first, css(token.second));
            dialog.setStyleSheet(sheet);
            for (const auto &icon : iconLabels)
                icon.first->setPixmap(svgPixmap(icon.second, k("--text-color"), 22));
            for (ToggleSwitch *toggle : toggles)
                toggle->setColors(k("--slider-bg"), k("--slider-bg-checked"), k("--text-color"));
            for (TickCheckBox *check : statusChecks)
                check->setColors(k("--background-color"), k("--border-color"), k("--slider-bg-checked"),
                                 k("--text-color"), k("--tab-font-color-inactive"));
        };
        restyle();

        // Everything applies (and is saved) the moment it changes, like the Tauri window.
        auto apply = [&] {
            m_wordWrap = wrap->isChecked();
            m_lineNumbers = numbers->isChecked();
            m_selectResults = selectResults->isChecked();
            const bool sessionChanged = m_restoreSession != restoreSession->isChecked();
            m_restoreSession = restoreSession->isChecked();
            m_baseFont = QFont(fontFamily->currentText());
            m_baseFont.setPointSizeF(fontSize->currentData().toInt() * 0.75);
            m_baseFont.setWeight(static_cast<QFont::Weight>(fontWeight->currentData().toInt()));
            m_skin = skin->currentData().toString();
            statusBar()->setVisible(status->isChecked());
            m_status = {statusChecks[0]->isChecked(), statusChecks[1]->isChecked(), statusChecks[2]->isChecked(),
                        statusChecks[3]->isChecked(), statusChecks[4]->isChecked(), statusChecks[5]->isChecked()};
            applyStatusItems();

            // Tabs and the default zoom reach the open editors too (only those still at the old default zoom).
            const int oldDefaultZoom = m_defaultZoom;
            m_defaultZoom = defaultZoom->currentData().toInt();
            m_zoomStep = zoomStep->currentData().toInt();
            m_tabSpaces = tabSpaces->currentData().toInt();
            forEachEditor([&](Editor *editor) {
                editor->setTabSpaces(m_tabSpaces);
                if (editor->zoomPercent == oldDefaultZoom && m_defaultZoom != oldDefaultZoom)
                    applyZoom(editor, m_defaultZoom);
            });

            m_settings.setValue(QStringLiteral("editor/wordWrap"), m_wordWrap);
            m_settings.setValue(QStringLiteral("editor/lineNumbers"), m_lineNumbers);
            m_settings.setValue(QStringLiteral("editor/selectResults"), m_selectResults);
            m_settings.setValue(QStringLiteral("editor/defaultZoom"), m_defaultZoom);
            m_settings.setValue(QStringLiteral("editor/zoomStep"), m_zoomStep);
            m_settings.setValue(QStringLiteral("editor/tabSpaces"), m_tabSpaces);
            m_settings.setValue(QStringLiteral("session/restore"), m_restoreSession);
            m_settings.setValue(QStringLiteral("editor/font"), m_baseFont);
            m_settings.setValue(QStringLiteral("appearance/skin"), m_skin);
            m_settings.setValue(QStringLiteral("view/statusBar"), status->isChecked());
            m_settings.setValue(QStringLiteral("status/path"), m_status.path);
            m_settings.setValue(QStringLiteral("status/cursor"), m_status.cursor);
            m_settings.setValue(QStringLiteral("status/chars"), m_status.chars);
            m_settings.setValue(QStringLiteral("status/words"), m_status.words);
            m_settings.setValue(QStringLiteral("status/zoom"), m_status.zoom);
            m_settings.setValue(QStringLiteral("status/lines"), m_status.lines);
            if (sessionChanged)
                saveSession(); // writes the session when turned on, deletes its files when turned off
            m_wrapAction->setChecked(m_wordWrap);
            m_lineNumbersAction->setChecked(m_lineNumbers);
            applyTheme();
            restyle();
        };
        for (ToggleSwitch *toggle : toggles)
            connect(toggle, &QAbstractButton::toggled, &dialog, apply);
        for (QCheckBox *check : statusChecks)
            connect(check, &QCheckBox::toggled, &dialog, apply);
        for (QComboBox *combo : {skin, fontFamily, fontSize, fontWeight, tabSpaces, defaultZoom, zoomStep})
            connect(combo, &QComboBox::currentIndexChanged, &dialog, apply);

        dialog.show();
        dialog.move(geometry().center() - QPoint(dialog.width() / 2, dialog.height() / 2));
        dialog.exec();
    }

    void openSkinsFolder() {
        const QString folder = QDir(appDataDirectory()).filePath(QStringLiteral("skins"));
        QDir().mkpath(folder);
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    }

    void revealCurrentFile() {
        Editor *editor = currentEditor();
        if (!editor || editor->path.isEmpty()) return;
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(editor->path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(editor->path).absolutePath()));
#endif
    }

    void readSecondInstance() {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QByteArray payload = socket->readAll();
                const QStringList paths = QString::fromUtf8(payload).split(QChar::Null, Qt::SkipEmptyParts);
                showNormal();
                raise();
                activateWindow();
                openFiles(paths);
                socket->disconnectFromServer();
                socket->deleteLater();
            });
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    QApplication::setApplicationName(QStringLiteral("Better Notepad"));
    QApplication::setOrganizationName(QStringLiteral("Pyrus"));
    QApplication::setOrganizationDomain(QStringLiteral("com.pyrus.better-notepad"));
    QLocalSocket existing;
    existing.connectToServer(kServerName, QIODevice::WriteOnly);
    if (existing.waitForConnected(250)) {
        const QStringList files = app.arguments().mid(1);
        QByteArray payload;
        for (const QString &path : files) {
            payload += path.toUtf8();
            payload += '\0';
        }
        existing.write(payload);
        existing.flush();
        existing.waitForBytesWritten(500);
        return 0;
    }

    BetterNotepad window(app.arguments().mid(1));
    window.show();
    return app.exec();
}

#include "native_main.moc"
