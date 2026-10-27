#pragma once
// The sort paths of PCManFM-Qt (libfm-qt), the KDE file dialogs (KIO) and
// Krusader, extracted for benchmarking. Each comparison follows the project's
// source line by line, with the project's file and model classes reduced to
// what the comparison reads. Snapshots and licenses:
//   libfm-qt 8e15258  src/proxyfoldermodel.cpp, src/foldermodel.cpp         LGPL-2.1-or-later
//   KIO      0b1b56b  src/filewidgets/kdirsortfilterproxymodel.cpp          LGPL-2.0-only
//   Krusader d20c6d0  app/Panel/PanelView/krsort.cpp, listmodel.cpp         GPL-2.0-or-later
// LGPL code may be relicensed under the GPL (LGPL-2.x section 3); the extracts
// are combined here under AGPL-3.0.
//
// Settings are each project's defaults: folders first, hidden files not last,
// case-insensitive natural (numeric) names in libfm-qt and KIO, Krusader's
// "Krusader" sort method (locale-aware, ignoring case). Hidden files are shown.

#include <QAbstractListModel>
#include <QAbstractTableModel>
#include <QCollator>
#include <QHash>
#include <QList>
#include <QSharedData>
#include <QSortFilterProxyModel>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <memory>
#include <utility>
#include <vector>

namespace qt_family {

/// One file of a dataset; each project wraps it in its own classes below.
struct File {
    int row = 0;  // position in the listing, which is also the row in the source models
    std::string path;
    QString name;
    QUrl url;
    bool is_dir = false;
    bool is_hidden = false;
    quint64 size = 0;
    qint64 mtime = 0;     // seconds
    qint64 mtime_ns = 0;  // nanoseconds since the epoch
};
using Item = File*;

enum Role { kName, kSize, kModified };
inline const char* role_name(int role) { return role == kName ? "name" : role == kSize ? "size" : "modified"; }

// ---------------------------------------------------------------------------
// PCManFM-Qt (libfm-qt): FolderModel + ProxyFolderModel (a QSortFilterProxyModel)
// ---------------------------------------------------------------------------
namespace pcmanfm {

/// The parts of Fm::FileInfo the comparison reads.
class FileInfo {
public:
    explicit FileInfo(const File* f) : file_(f), displayName_(f->name) {}
    bool isDir() const { return file_->is_dir; }
    bool isHidden() const { return file_->is_hidden; }
    quint64 size() const { return file_->size; }
    quint64 mtime() const { return static_cast<quint64>(file_->mtime); }
    const QString& displayName() const { return displayName_; }
    const File* file() const { return file_; }

private:
    const File* file_;
    QString displayName_;
};

struct FolderModelItem {
    std::shared_ptr<const FileInfo> info;
    const QString& displayName() const { return info->displayName(); }
};

class FolderModel : public QAbstractListModel {
public:
    enum ColumnId {
        ColumnFileName,
        ColumnFileType,
        ColumnFileSize,
        ColumnFileMTime,
        ColumnFileCrTime,
        ColumnFileDTime,
        ColumnFileOwner,
        ColumnFileGroup,
        NumOfColumns
    };

    explicit FolderModel(const std::vector<Item>& files) {
        items.reserve(static_cast<qsizetype>(files.size()));
        for (Item f : files) {
            items.append(FolderModelItem{std::make_shared<const FileInfo>(f)});
        }
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(items.size());
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : NumOfColumns;
    }
    QModelIndex index(int row, int column, const QModelIndex& = QModelIndex()) const override {
        if (row < 0 || row >= items.size() || column < 0 || column >= NumOfColumns) {
            return QModelIndex();
        }
        const FolderModelItem& item = items.at(row);
        return createIndex(row, column, (void*)&item);
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() > items.size() || index.column() >= NumOfColumns) {
            return QVariant();
        }
        const FolderModelItem* item = itemFromIndex(index);
        if (role == Qt::DisplayRole && index.column() == ColumnFileName) {
            return item->displayName();
        }
        return QVariant();
    }
    FolderModelItem* itemFromIndex(const QModelIndex& index) const {
        return reinterpret_cast<FolderModelItem*>(index.internalPointer());
    }
    std::shared_ptr<const FileInfo> fileInfoFromIndex(const QModelIndex& index) const {
        FolderModelItem* item = itemFromIndex(index);
        return item ? item->info : nullptr;
    }

    QList<FolderModelItem> items;
};

class ProxyFolderModel : public QSortFilterProxyModel {
public:
    ProxyFolderModel() {
        setDynamicSortFilter(true);
        setSortCaseSensitivity(Qt::CaseInsensitive);
        collator_.setNumericMode(true);
    }
    void setSortCaseSensitivity(Qt::CaseSensitivity cs) {
        collator_.setCaseSensitivity(cs);
        QSortFilterProxyModel::setSortCaseSensitivity(cs);
    }
    /// ProxyFolderModel::lessThan (). `order` stands for sortOrder () when the
    /// comparison is called outside a sort (for checking orders).
    bool lessThanFor(const QModelIndex& left, const QModelIndex& right, int column, Qt::SortOrder order) const {
        auto* srcModel = static_cast<FolderModel*>(sourceModel());
        auto leftInfo = srcModel->fileInfoFromIndex(left);
        auto rightInfo = srcModel->fileInfoFromIndex(right);

        if (folderFirst_) {
            bool leftIsFolder = leftInfo->isDir();
            bool rightIsFolder = rightInfo->isDir();
            if (leftIsFolder != rightIsFolder) {
                return order == Qt::AscendingOrder ? leftIsFolder : rightIsFolder;
            }
        }
        if (hiddenLast_) {
            bool leftIsHidden = leftInfo->isHidden();
            bool rightIsHidden = rightInfo->isHidden();
            if (leftIsHidden != rightIsHidden) {
                return order == Qt::AscendingOrder ? rightIsHidden : leftIsHidden;
            }
        }

        int comp = 0;
        switch (column) {
            case FolderModel::ColumnFileMTime:
                if (leftInfo->mtime() != rightInfo->mtime()) {
                    return leftInfo->mtime() < rightInfo->mtime();
                }
                break;
            case FolderModel::ColumnFileSize:
                if (leftInfo->size() != rightInfo->size()) {
                    return leftInfo->size() < rightInfo->size();
                }
                break;
            default: {
                // To have a more natural sorting like that of GTK, we consider dot
                // as a separator and compare sub-strings from left to right.
                QString leftText = left.data(Qt::DisplayRole).toString();
                QString rightText = right.data(Qt::DisplayRole).toString();
                qsizetype leftStart = 0, rightStart = 0;
                qsizetype leftEnd = 0, rightEnd = 0;
                for (;;) {
                    leftEnd = leftText.indexOf(QLatin1Char('.'), leftStart);
                    rightEnd = rightText.indexOf(QLatin1Char('.'), rightStart);
                    QString lefPart = leftEnd == -1 ? leftText.sliced(leftStart, leftText.size() - leftStart)
                                                    : leftText.sliced(leftStart, leftEnd - leftStart);
                    QString rightPart = rightEnd == -1 ? rightText.sliced(rightStart, rightText.size() - rightStart)
                                                       : rightText.sliced(rightStart, rightEnd - rightStart);
                    comp = collator_.compare(lefPart, rightPart);
                    if (comp == 0) {
                        // QCollator considers "A0" and "A00" equal in numeric mode.
                        comp = static_cast<int>(lefPart.size() - rightPart.size());
                    }
                    if (comp != 0 || leftEnd == -1 || rightEnd == -1) {
                        break;
                    }
                    leftStart = leftEnd + 1;
                    rightStart = rightEnd + 1;
                }
                if (comp == 0) {
                    comp = static_cast<int>(leftEnd - rightEnd);  // covers all remaining cases
                }
                break;
            }
        }
        // always sort files by their display names when they have the same property
        if (comp == 0) {
            return collator_.compare(leftInfo->displayName(), rightInfo->displayName()) < 0;
        }
        return comp < 0;
    }

    /// The first dot-separated part of a name: what the name comparison looks at first.
    static QStringView firstPart(const QString& name) {
        const qsizetype dot = name.indexOf(QLatin1Char('.'));
        return dot == -1 ? QStringView(name) : QStringView(name).left(dot);
    }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        return lessThanFor(left, right, sortColumn(), sortOrder());
    }

private:
    bool folderFirst_ = true;
    bool hiddenLast_ = false;
    QCollator collator_;
};

inline int column_of(Role role) {
    return role == kName ? FolderModel::ColumnFileName : role == kSize ? FolderModel::ColumnFileSize : FolderModel::ColumnFileMTime;
}

}  // namespace pcmanfm

// ---------------------------------------------------------------------------
// KDE file dialogs (KIO): KDirModel + KDirSortFilterProxyModel
// ---------------------------------------------------------------------------
namespace kio {

/// KIO::UDSEntry keeps numeric fields in a vector and finds them linearly.
class UDSEntry {
public:
    enum : unsigned {
        UDS_SIZE = 0x10000 | 1,
        UDS_USER = 0x01000000 | 2,
        UDS_ICON_NAME = 0x01000000 | 3,
        UDS_GROUP = 0x01000000 | 4,
        UDS_NAME = 0x01000000 | 5,
        UDS_ACCESS = 0x02000000 | 8,
        UDS_MODIFICATION_TIME = 0x02000000 | 9,
        UDS_ACCESS_TIME = 0x02000000 | 10,
        UDS_CREATION_TIME = 0x02000000 | 11,
        UDS_FILE_TYPE = 0x02000000 | 12,
        UDS_DEVICE_ID = 0x02000000 | 26,
        UDS_INODE = 0x02000000 | 27,
        UDS_MODIFICATION_TIME_NS_OFFSET = 0x02000000 | 30,
    };
    void fastInsert(unsigned field, long long value) { numberStorage.push_back({field, value}); }
    long long numberValue(unsigned field, long long defaultValue = -1) const {
        auto it = std::find_if(numberStorage.cbegin(), numberStorage.cend(),
                               [field](const NumberField& entry) { return entry.m_index == field; });
        return it != numberStorage.cend() ? it->m_long : defaultValue;
    }

private:
    struct NumberField {
        unsigned m_index;
        long long m_long;
    };
    std::vector<NumberField> numberStorage;
};

/// The parts of KFileItem the comparison reads. KFileItem is implicitly shared.
class KFileItem {
public:
    KFileItem() = default;
    explicit KFileItem(const File* f) : d(new Private) {
        d->file = f;
        d->m_strName = f->name;
        d->m_strText = f->name;
        d->m_url = f->url;
        // The numeric fields of a local file's entry, in the order the file worker adds them.
        d->m_entry.fastInsert(UDSEntry::UDS_FILE_TYPE, f->is_dir ? 040000 : 0100000);
        d->m_entry.fastInsert(UDSEntry::UDS_ACCESS, 0644);
        d->m_entry.fastInsert(UDSEntry::UDS_SIZE, static_cast<long long>(f->size));
        d->m_entry.fastInsert(UDSEntry::UDS_DEVICE_ID, 2049);
        d->m_entry.fastInsert(UDSEntry::UDS_INODE, 1234567);
        d->m_entry.fastInsert(UDSEntry::UDS_MODIFICATION_TIME, f->mtime);
        d->m_entry.fastInsert(UDSEntry::UDS_MODIFICATION_TIME_NS_OFFSET, f->mtime_ns - f->mtime * 1000000000LL);
        d->m_entry.fastInsert(UDSEntry::UDS_ACCESS_TIME, f->mtime);
        d->m_entry.fastInsert(UDSEntry::UDS_CREATION_TIME, f->mtime);
    }
    bool isNull() const { return !d; }
    bool isDir() const { return d->file->is_dir; }
    bool isHidden() const { return d->m_strName.startsWith(QLatin1Char('.')); }
    quint64 size() const { return d->file->size; }
    QString text() const { return d->m_strText; }
    QString name(bool lowerCase = false) const {
        if (!lowerCase) {
            return d->m_strName;
        }
        if (d->m_strLowerCaseName.isNull()) {
            d->m_strLowerCaseName = d->m_strName.toLower();
        }
        return d->m_strLowerCaseName;
    }
    QUrl url() const { return d->m_url; }
    const UDSEntry& entry() const { return d->m_entry; }

private:
    struct Private : QSharedData {
        const File* file = nullptr;
        QString m_strName;
        QString m_strText;
        mutable QString m_strLowerCaseName;
        QUrl m_url;
        UDSEntry m_entry;
    };
    QExplicitlySharedDataPointer<Private> d;
};

class KDirModel : public QAbstractTableModel {
public:
    enum ModelColumns { Name = 0, Size, ModifiedTime, Permissions, Owner, Group, Type, ColumnCount };
    enum AdditionalRoles { ChildCountRole = 0x2C1F7C5D };  // KDirModel::ChildCountRole
    enum { ChildCountUnknown = -1 };

    explicit KDirModel(const std::vector<Item>& files) {
        for (Item f : files) {
            items_.push_back(KFileItem(f));
        }
    }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : static_cast<int>(items_.size());
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override { return parent.isValid() ? 0 : ColumnCount; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (role == ChildCountRole) {
            // Folder item counts are not known in a fresh view (for local folders
            // KDirModel counts them with readdir() the first time they are asked for).
            return int(ChildCountUnknown);
        }
        if (role == Qt::DisplayRole && index.column() == Name) {
            return items_[static_cast<std::size_t>(index.row())].text();
        }
        return QVariant();
    }
    KFileItem itemForIndex(const QModelIndex& index) const { return items_[static_cast<std::size_t>(index.row())]; }

private:
    std::vector<KFileItem> items_;
};

class KDirSortFilterProxyModel : public QSortFilterProxyModel {
public:
    KDirSortFilterProxyModel() {
        m_collator.setNumericMode(m_naturalSorting);
        setDynamicSortFilter(true);
        setSortCaseSensitivity(Qt::CaseInsensitive);
    }

    int compare(const QString& a, const QString& b, Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive) const {
        int result;
        if (m_naturalSorting) {
            m_collator.setCaseSensitivity(caseSensitivity);
            result = m_collator.compare(a, b);
        } else {
            result = QString::compare(a, b, caseSensitivity);
        }
        if (caseSensitivity == Qt::CaseSensitive || result != 0) {
            return result;
        }
        return QString::compare(a, b, Qt::CaseSensitive);
    }

    /// KDirSortFilterProxyModel::subSortLessThan (); `order` stands for sortOrder ().
    bool subSortLessThanFor(const QModelIndex& left, const QModelIndex& right, Qt::SortOrder order) const {
        auto* dirModel = static_cast<KDirModel*>(sourceModel());
        const KFileItem leftFileItem = dirModel->itemForIndex(left);
        const KFileItem rightFileItem = dirModel->itemForIndex(right);
        const bool isLessThan = (order == Qt::AscendingOrder);

        if (m_sortHiddenFilesLast) {
            const bool leftItemIsHidden = leftFileItem.isHidden();
            const bool rightItemIsHidden = rightFileItem.isHidden();
            if (leftItemIsHidden && !rightItemIsHidden) {
                return !isLessThan;
            } else if (!leftItemIsHidden && rightItemIsHidden) {
                return isLessThan;
            }
        }
        if (m_sortFoldersFirst) {
            const bool leftItemIsDir = leftFileItem.isDir();
            const bool rightItemIsDir = rightFileItem.isDir();
            if (leftItemIsDir && !rightItemIsDir) {
                return isLessThan;
            } else if (!leftItemIsDir && rightItemIsDir) {
                return !isLessThan;
            }
        }

        switch (left.column()) {
            case KDirModel::Name: {
                int result = compare(leftFileItem.text(), rightFileItem.text(), sortCaseSensitivity());
                if (result == 0) {
                    result = compare(leftFileItem.name(sortCaseSensitivity() == Qt::CaseInsensitive),
                                     rightFileItem.name(sortCaseSensitivity() == Qt::CaseInsensitive), sortCaseSensitivity());
                    if (result == 0) {
                        result = compare(leftFileItem.url().toString(), rightFileItem.url().toString(), sortCaseSensitivity());
                    }
                }
                return result < 0;
            }
            case KDirModel::Size: {
                if (leftFileItem.isDir() && rightFileItem.isDir()) {
                    QVariant leftValue = dirModel->data(left, KDirModel::ChildCountRole);
                    int leftCount = (leftValue.typeId() == QMetaType::Int) ? leftValue.toInt() : KDirModel::ChildCountUnknown;
                    QVariant rightValue = dirModel->data(right, KDirModel::ChildCountRole);
                    int rightCount = (rightValue.typeId() == QMetaType::Int) ? rightValue.toInt() : KDirModel::ChildCountUnknown;
                    if (leftCount == rightCount) {
                        return compare(leftFileItem.text(), rightFileItem.text(), sortCaseSensitivity()) < 0;
                    }
                    if (leftCount == KDirModel::ChildCountUnknown) {
                        return false;
                    }
                    if (rightCount == KDirModel::ChildCountUnknown) {
                        return true;
                    }
                    return leftCount < rightCount;
                }
                if (leftFileItem.size() == rightFileItem.size()) {
                    return compare(leftFileItem.text(), rightFileItem.text(), sortCaseSensitivity()) < 0;
                }
                return leftFileItem.size() < rightFileItem.size();
            }
            case KDirModel::ModifiedTime: {
                const auto leftModifiedTime = leftFileItem.entry().numberValue(UDSEntry::UDS_MODIFICATION_TIME);
                const auto rightModifiedTime = rightFileItem.entry().numberValue(UDSEntry::UDS_MODIFICATION_TIME);
                if (leftModifiedTime == rightModifiedTime) {
                    const auto leftNs = leftFileItem.entry().numberValue(UDSEntry::UDS_MODIFICATION_TIME_NS_OFFSET);
                    const auto rightNs = rightFileItem.entry().numberValue(UDSEntry::UDS_MODIFICATION_TIME_NS_OFFSET);
                    if (leftNs == rightNs) {
                        return compare(leftFileItem.text(), rightFileItem.text(), sortCaseSensitivity()) < 0;
                    }
                    return leftNs < rightNs;
                }
                return leftModifiedTime < rightModifiedTime;
            }
        }
        return false;
    }

protected:
    /// KCategorizedSortFilterProxyModel::lessThan () without categories.
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        return subSortLessThanFor(left, right, sortOrder());
    }

private:
    bool m_sortFoldersFirst = true;
    bool m_sortHiddenFilesLast = false;  // DefaultHiddenFilesLast
    bool m_naturalSorting = true;        // KDE/NaturalSorting
    mutable QCollator m_collator;
};

inline int column_of(Role role) {
    return role == kName ? KDirModel::Name : role == kSize ? KDirModel::Size : KDirModel::ModifiedTime;
}

}  // namespace kio

// ---------------------------------------------------------------------------
// Krusader: KrSort::Sorter (std::stable_sort) and ListModel::sort ()
// ---------------------------------------------------------------------------
namespace krusader {

struct KrViewProperties {
    enum SortSpec { Name = 0x0, Ext = 0x1, Size = 0x2, Type = 0x3, Modified = 0x4, Changed = 0x5, Accessed = 0x6,
                    Permissions = 0x7, KrPermissions = 0x8, Owner = 0x9, Group = 0xa };
    enum SortOptions { Descending = 0x200, DirsFirst = 0x400, IgnoreCase = 0x800, AlwaysSortDirsByName = 0x1000,
                       LocaleAwareSort = 0x2000 };
    enum SortMethod { Alphabetical = 0x1, AlphabeticalNumbers = 0x2, CharacterCode = 0x4, CharacterCodeNumbers = 0x8,
                      Krusader = 0x10 };
    int sortColumn = Name;
    int sortOptions = DirsFirst | IgnoreCase | LocaleAwareSort;
    SortMethod sortMethod = Krusader;
    bool localeAwareCompareIsCaseSensitive = QString(QStringLiteral("a")).localeAwareCompare(QStringLiteral("B")) > 0;
};

/// The parts of Krusader's FileItem the comparison reads.
struct FileItem {
    const File* file;
    const QString& getName() const { return file->name; }
    bool isDir() const { return file->is_dir; }
    quint64 getSize() const { return file->size; }
    time_t getModificationTime() const { return static_cast<time_t>(file->mtime); }
    const QUrl& getUrl() const { return file->url; }
};

class SortProps {
public:
    SortProps() = default;
    SortProps(FileItem* fileitem, int col, const KrViewProperties* props, bool isDummy, bool asc, int origNdx) {
        init(fileitem, col, props, isDummy, asc, origNdx);
    }
    void init(FileItem* fileitem, int col, const KrViewProperties* props, bool isDummy, bool asc, int origNdx) {
        _col = col;
        _prop = props;
        _isdummy = isDummy;
        _ascending = asc;
        _fileItem = fileitem;
        _index = origNdx;
        _name = fileitem->getName();
        if (_prop->sortOptions & KrViewProperties::IgnoreCase) {
            _name = _name.toLower();
        }
    }
    int column() const { return _col; }
    const KrViewProperties* properties() const { return _prop; }
    bool isDummy() const { return _isdummy; }
    bool isAscending() const { return _ascending; }
    QString name() const { return _name; }
    FileItem* fileitem() const { return _fileItem; }
    int originalIndex() const { return _index; }

private:
    int _col = 0;
    const KrViewProperties* _prop = nullptr;
    bool _isdummy = false;
    FileItem* _fileItem = nullptr;
    bool _ascending = true;
    QString _name;
    int _index = 0;
};

inline bool compareTextsKrusader(const QString& leftString, const QString& rightString, const KrViewProperties* props) {
    if (((props->sortOptions & KrViewProperties::IgnoreCase) || props->localeAwareCompareIsCaseSensitive) &&
        (props->sortOptions & KrViewProperties::LocaleAwareSort)) {
        return QString::localeAwareCompare(leftString, rightString) < 0;
    }
    return QString::compare(leftString, rightString) < 0;
}

inline bool compareTexts(QString leftString, QString rightString, const KrViewProperties* props, bool asc, bool isName) {
    if (leftString.isEmpty() && rightString.isEmpty()) {
        return false;
    } else if (leftString.isEmpty()) {
        return true;
    } else if (rightString.isEmpty()) {
        return false;
    }
    if (isName) {
        if (leftString == QLatin1String("..")) {
            return !asc;
        } else if (rightString == QLatin1String("..")) {
            return asc;
        }
    }
    // Only the default sort method ("Krusader") is benchmarked.
    return compareTextsKrusader(leftString, rightString, props);
}

inline bool compareTime(time_t time1, time_t time2, SortProps* sp, SortProps* sp2) {
    return time1 != time2 ? time1 < time2 : compareTexts(sp->name(), sp2->name(), sp->properties(), sp->isAscending(), true);
}

inline bool itemLessThan(SortProps* sp, SortProps* sp2) {
    FileItem* file1 = sp->fileitem();
    FileItem* file2 = sp2->fileitem();
    bool isdir1 = file1->isDir();
    bool isdir2 = file2->isDir();
    bool dirsFirst = sp->properties()->sortOptions & KrViewProperties::DirsFirst;
    bool alwaysSortDirsByName =
        sp->properties()->sortOptions & KrViewProperties::AlwaysSortDirsByName && dirsFirst && isdir1 && isdir2;

    if (dirsFirst) {
        if (isdir1 && !isdir2) return sp->isAscending();
        if (isdir2 && !isdir1) return !sp->isAscending();
    }
    if (sp->isDummy()) return sp->isAscending();
    if (sp2->isDummy()) return !sp->isAscending();

    int column = sp->column();
    if (dirsFirst && isdir1 && isdir2 && alwaysSortDirsByName) {
        alwaysSortDirsByName = !sp->isAscending();
        column = KrViewProperties::Name;
    }
    switch (column) {
        case KrViewProperties::Name:
            return compareTexts(sp->name(), sp2->name(), sp->properties(), sp->isAscending(), true) ^ alwaysSortDirsByName;
        case KrViewProperties::Size:
            if (file1->getSize() == file2->getSize()) {
                return compareTexts(sp->name(), sp2->name(), sp->properties(), sp->isAscending(), true);
            }
            return file1->getSize() < file2->getSize();
        case KrViewProperties::Modified:
            return compareTime(file1->getModificationTime(), file2->getModificationTime(), sp, sp2);
    }
    return sp->name() < sp2->name();
}

/// Krusader's descending comparison. It returns true for items that compare
/// equal (and for an item with itself), so it is not a strict weak ordering.
inline bool itemGreaterThan(SortProps* sp, SortProps* sp2) { return !itemLessThan(sp, sp2); }

/// KrSort::Sorter.
class Sorter {
public:
    using LessThanFunc = bool (*)(SortProps*, SortProps*);
    Sorter(qsizetype reserveItems, const KrViewProperties* viewProperties)
        : _viewProperties(viewProperties) {
        _items.reserve(reserveItems);
        _itemStore.reserve(reserveItems);
    }
    void addItem(FileItem* fileitem, bool isDummy, int idx) {
        _itemStore << SortProps(fileitem, _viewProperties->sortColumn, _viewProperties, isDummy, !descending(), idx);
        _items << &_itemStore.last();
    }
    void sort() { std::stable_sort(_items.begin(), _items.end(), descending() ? itemGreaterThan : itemLessThan); }
    const QVector<SortProps*>& items() const { return _items; }
    bool descending() const { return _viewProperties->sortOptions & KrViewProperties::Descending; }

private:
    const KrViewProperties* _viewProperties;
    QVector<SortProps*> _items;
    QVector<SortProps> _itemStore;
};

/// ListModel::sort () without the view: build a Sorter for all items, sort,
/// then rebuild the item list, its three lookup hashes and the change map.
class ListModel : public QAbstractListModel {
public:
    explicit ListModel(std::vector<FileItem>& items) {
        for (FileItem& item : items) {
            _fileItems.append(&item);
        }
    }
    int rowCount(const QModelIndex& = QModelIndex()) const override { return static_cast<int>(_fileItems.count()); }
    QVariant data(const QModelIndex&, int) const override { return QVariant(); }

    void setItems(const std::vector<FileItem*>& order) { _fileItems = QList<FileItem*>(order.begin(), order.end()); }
    const QList<FileItem*>& fileItems() const { return _fileItems; }

    void sort(const KrViewProperties* properties) {
        emit layoutAboutToBeChanged();
        QModelIndexList oldPersistentList = persistentIndexList();
        Sorter sorter(_fileItems.count(), properties);
        for (int i = 0; i < _fileItems.count(); i++) {
            sorter.addItem(_fileItems[i], false, i);
        }
        sorter.sort();
        _fileItems.clear();
        _fileItemNdx.clear();
        _nameNdx.clear();
        _urlNdx.clear();
        QHash<int, int> changeMap;
        for (int i = 0; i < sorter.items().count(); ++i) {
            const SortProps* props = sorter.items()[i];
            _fileItems.append(props->fileitem());
            changeMap[props->originalIndex()] = i;
            updateIndices(props->fileitem(), i);
        }
        QModelIndexList newPersistentList;
        for (const QModelIndex& mndx : std::as_const(oldPersistentList)) {
            newPersistentList << index(changeMap[mndx.row()], mndx.column());
        }
        changePersistentIndexList(oldPersistentList, newPersistentList);
        emit layoutChanged();
    }

private:
    void updateIndices(FileItem* file, int i) {
        _fileItemNdx[file] = index(i, 0);
        _nameNdx[file->getName()] = index(i, 0);
        _urlNdx[file->getUrl()] = index(i, 0);
    }

    QList<FileItem*> _fileItems;
    QHash<FileItem*, QModelIndex> _fileItemNdx;
    QHash<QString, QModelIndex> _nameNdx;
    QHash<QUrl, QModelIndex> _urlNdx;
};

inline int column_of(Role role) {
    return role == kName ? KrViewProperties::Name : role == kSize ? KrViewProperties::Size : KrViewProperties::Modified;
}

}  // namespace krusader

}  // namespace qt_family
