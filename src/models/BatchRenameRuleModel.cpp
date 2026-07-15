#include "BatchRenameRuleModel.h"

BatchRenameRuleModel::BatchRenameRuleModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_rules.append(BatchRenameRule{});
}

int BatchRenameRuleModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rules.size();
}

QVariant BatchRenameRuleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size()) return {};
    const BatchRenameRule &rule = m_rules.at(index.row());
    const QVariantMap map = toMap(rule);
    switch (role) {
    case TypeRole: return map.value(QStringLiteral("type"));
    case SearchRole: return rule.search;
    case ReplacementRole: return rule.replacement;
    case CaseSensitiveRole: return rule.caseSensitive;
    case RegexRole: return rule.regex;
    case PrefixRole: return rule.prefix;
    case SuffixRole: return rule.suffix;
    case StartRole: return rule.start;
    case PaddingRole: return rule.padding;
    case PositionRole: return rule.position;
    case TextRole: return rule.text;
    case SequenceStartRole: return rule.sequenceStart;
    case SequencePaddingRole: return rule.sequencePadding;
    case ModeRole: return rule.mode;
    case TitleRole: return title(rule);
    case SummaryRole: return summary(rule);
    default: return {};
    }
}

QHash<int, QByteArray> BatchRenameRuleModel::roleNames() const
{
    return {{TypeRole, "type"}, {SearchRole, "search"}, {ReplacementRole, "replacement"},
            {CaseSensitiveRole, "caseSensitive"}, {RegexRole, "regex"}, {PrefixRole, "prefix"},
            {SuffixRole, "suffixText"}, {StartRole, "start"}, {PaddingRole, "padding"},
            {PositionRole, "position"}, {TextRole, "text"}, {SequenceStartRole, "seqStart"},
            {SequencePaddingRole, "seqPadding"}, {ModeRole, "mode"}, {TitleRole, "title"},
            {SummaryRole, "summary"}};
}

QVariantMap BatchRenameRuleModel::get(int index) const
{
    return index >= 0 && index < m_rules.size() ? toMap(m_rules.at(index)) : QVariantMap{};
}

int BatchRenameRuleModel::addRule(const QString &type)
{
    BatchRenameRule rule;
    rule.type = typeFromString(type);
    const int index = m_rules.size();
    beginInsertRows({}, index, index);
    m_rules.append(rule);
    endInsertRows();
    emit countChanged();
    emit rulesChanged();
    return index;
}

bool BatchRenameRuleModel::removeRule(int index)
{
    if (m_rules.size() <= 1 || index < 0 || index >= m_rules.size()) return false;
    beginRemoveRows({}, index, index);
    m_rules.removeAt(index);
    endRemoveRows();
    emit countChanged();
    emit rulesChanged();
    return true;
}

bool BatchRenameRuleModel::setRule(int index, const QVariantMap &values)
{
    if (index < 0 || index >= m_rules.size()) return false;
    const BatchRenameRule updated = fromMap(values, m_rules.at(index));
    m_rules[index] = updated;
    emit dataChanged(this->index(index), this->index(index));
    emit rulesChanged();
    return true;
}

bool BatchRenameRuleModel::moveRule(int from, int to)
{
    if (from < 0 || from >= m_rules.size() || to < 0 || to >= m_rules.size() || from == to) return false;
    const int destination = to > from ? to + 1 : to;
    beginMoveRows({}, from, from, {}, destination);
    m_rules.move(from, to);
    endMoveRows();
    emit rulesChanged();
    return true;
}

void BatchRenameRuleModel::resetToDefault()
{
    beginResetModel();
    m_rules = {BatchRenameRule{}};
    endResetModel();
    emit countChanged();
    emit rulesChanged();
}

QVariantList BatchRenameRuleModel::engineRules() const
{
    QVariantList result;
    for (const BatchRenameRule &rule : m_rules) {
        QVariantMap value{{QStringLiteral("type"), typeName(rule.type)}};
        switch (rule.type) {
        case BatchRenameRule::Type::Replace:
            value.insert(QStringLiteral("search"), rule.search);
            value.insert(QStringLiteral("replace"), rule.replacement);
            value.insert(QStringLiteral("caseSensitive"), rule.caseSensitive);
            value.insert(QStringLiteral("regex"), rule.regex);
            break;
        case BatchRenameRule::Type::Format:
            value.insert(QStringLiteral("prefix"), rule.prefix);
            value.insert(QStringLiteral("suffix"), rule.suffix);
            break;
        case BatchRenameRule::Type::Numbering:
            value.insert(QStringLiteral("start"), rule.start);
            value.insert(QStringLiteral("padding"), rule.padding);
            value.insert(QStringLiteral("position"), rule.position);
            break;
        case BatchRenameRule::Type::Template:
            value.insert(QStringLiteral("text"), rule.text);
            value.insert(QStringLiteral("start"), rule.sequenceStart);
            value.insert(QStringLiteral("padding"), rule.sequencePadding);
            break;
        case BatchRenameRule::Type::Transform:
            value.insert(QStringLiteral("mode"), rule.mode);
            break;
        }
        result.append(value);
    }
    return result;
}

BatchRenameRule::Type BatchRenameRuleModel::typeFromString(const QString &type)
{
    if (type == QLatin1String("format")) return BatchRenameRule::Type::Format;
    if (type == QLatin1String("numbering")) return BatchRenameRule::Type::Numbering;
    if (type == QLatin1String("template")) return BatchRenameRule::Type::Template;
    if (type == QLatin1String("transform")) return BatchRenameRule::Type::Transform;
    return BatchRenameRule::Type::Replace;
}

QString BatchRenameRuleModel::typeName(BatchRenameRule::Type type)
{
    switch (type) {
    case BatchRenameRule::Type::Format: return QStringLiteral("format");
    case BatchRenameRule::Type::Numbering: return QStringLiteral("numbering");
    case BatchRenameRule::Type::Template: return QStringLiteral("template");
    case BatchRenameRule::Type::Transform: return QStringLiteral("transform");
    default: return QStringLiteral("replace");
    }
}

QVariantMap BatchRenameRuleModel::toMap(const BatchRenameRule &rule)
{
    return {{QStringLiteral("type"), typeName(rule.type)}, {QStringLiteral("search"), rule.search},
            {QStringLiteral("replace"), rule.replacement}, {QStringLiteral("caseSensitive"), rule.caseSensitive},
            {QStringLiteral("regex"), rule.regex}, {QStringLiteral("prefix"), rule.prefix},
            {QStringLiteral("suffixText"), rule.suffix}, {QStringLiteral("start"), rule.start},
            {QStringLiteral("padding"), rule.padding}, {QStringLiteral("position"), rule.position},
            {QStringLiteral("text"), rule.text}, {QStringLiteral("seqStart"), rule.sequenceStart},
            {QStringLiteral("seqPadding"), rule.sequencePadding}, {QStringLiteral("mode"), rule.mode}};
}

BatchRenameRule BatchRenameRuleModel::fromMap(const QVariantMap &values, const BatchRenameRule &fallback)
{
    BatchRenameRule rule = fallback;
    if (values.contains(QStringLiteral("type"))) rule.type = typeFromString(values.value(QStringLiteral("type")).toString());
    if (values.contains(QStringLiteral("search"))) rule.search = values.value(QStringLiteral("search")).toString();
    if (values.contains(QStringLiteral("replace"))) rule.replacement = values.value(QStringLiteral("replace")).toString();
    if (values.contains(QStringLiteral("caseSensitive"))) rule.caseSensitive = values.value(QStringLiteral("caseSensitive")).toBool();
    if (values.contains(QStringLiteral("regex"))) rule.regex = values.value(QStringLiteral("regex")).toBool();
    if (values.contains(QStringLiteral("prefix"))) rule.prefix = values.value(QStringLiteral("prefix")).toString();
    if (values.contains(QStringLiteral("suffixText"))) rule.suffix = values.value(QStringLiteral("suffixText")).toString();
    if (values.contains(QStringLiteral("start"))) rule.start = values.value(QStringLiteral("start")).toInt();
    if (values.contains(QStringLiteral("padding"))) rule.padding = values.value(QStringLiteral("padding")).toInt();
    if (values.contains(QStringLiteral("position"))) rule.position = values.value(QStringLiteral("position")).toString();
    if (values.contains(QStringLiteral("text"))) rule.text = values.value(QStringLiteral("text")).toString();
    if (values.contains(QStringLiteral("seqStart"))) rule.sequenceStart = values.value(QStringLiteral("seqStart")).toInt();
    if (values.contains(QStringLiteral("seqPadding"))) rule.sequencePadding = values.value(QStringLiteral("seqPadding")).toInt();
    if (values.contains(QStringLiteral("mode"))) rule.mode = values.value(QStringLiteral("mode")).toString();
    return rule;
}

QString BatchRenameRuleModel::title(const BatchRenameRule &rule)
{
    switch (rule.type) {
    case BatchRenameRule::Type::Format: return QStringLiteral("Format");
    case BatchRenameRule::Type::Numbering: return QStringLiteral("Numbering");
    case BatchRenameRule::Type::Template: return QStringLiteral("Sequence");
    case BatchRenameRule::Type::Transform: return QStringLiteral("Transform");
    default: return rule.regex ? QStringLiteral("Regex Replace") : QStringLiteral("Search & Replace");
    }
}

QString BatchRenameRuleModel::summary(const BatchRenameRule &rule)
{
    static const QStringList transformLabels = {QStringLiteral("lowercase"), QStringLiteral("UPPERCASE"),
        QStringLiteral("Title Case"), QStringLiteral("Trim whitespace"), QStringLiteral("Collapse spaces"),
        QStringLiteral("Spaces to underscores"), QStringLiteral("Spaces to dashes"), QStringLiteral("Remove special chars")};
    static const QStringList transformModes = {QStringLiteral("lowercase"), QStringLiteral("uppercase"),
        QStringLiteral("titlecase"), QStringLiteral("trim"), QStringLiteral("collapse-spaces"),
        QStringLiteral("spaces-underscore"), QStringLiteral("spaces-dash"), QStringLiteral("remove-special")};
    switch (rule.type) {
    case BatchRenameRule::Type::Format: return rule.prefix + QStringLiteral("name") + rule.suffix;
    case BatchRenameRule::Type::Numbering: return rule.position + QStringLiteral(" from ") + QString::number(rule.start);
    case BatchRenameRule::Type::Template: return (rule.text.isEmpty() ? QStringLiteral("Name") : rule.text) + QStringLiteral(" + number");
    case BatchRenameRule::Type::Transform: {
        const int index = transformModes.indexOf(rule.mode);
        return transformLabels.value(index < 0 ? 0 : index);
    }
    default: return (rule.search.isEmpty() ? QStringLiteral("text") : rule.search) + QStringLiteral(" -> ") + rule.replacement;
    }
}
