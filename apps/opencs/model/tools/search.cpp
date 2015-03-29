
#include "search.hpp"

#include <stdexcept>
#include <sstream>

#include "../../model/doc/messages.hpp"

#include "../../model/world/idtablebase.hpp"
#include "../../model/world/columnbase.hpp"
#include "../../model/world/universalid.hpp"

void CSMTools::Search::searchTextCell (const CSMWorld::IdTableBase *model,
    const QModelIndex& index, const CSMWorld::UniversalId& id,
    CSMDoc::Messages& messages) const
{
    // using QString here for easier handling of case folding.
    
    QString search = QString::fromUtf8 (mText.c_str());
    QString text = model->data (index).toString();

    int pos = 0;

    while ((pos = text.indexOf (search, pos, Qt::CaseInsensitive))!=-1)
    {
        std::ostringstream hint;
        hint
            << "r: "
            << model->getColumnId (index.column())
            << " " << pos
            << " " << search.length();
        
        messages.add (id, formatDescription (text, pos, search.length()).toUtf8().data(), hint.str());

        pos += search.length();
    }
}

void CSMTools::Search::searchRegExCell (const CSMWorld::IdTableBase *model,
    const QModelIndex& index, const CSMWorld::UniversalId& id,
    CSMDoc::Messages& messages) const
{
    QString text = model->data (index).toString();

    int pos = 0;

    while ((pos = mRegExp.indexIn (text, pos))!=-1)
    {
        int length = mRegExp.matchedLength();
        
        std::ostringstream hint;
        hint << "r: " << model->getColumnId (index.column()) << " " << pos << " " << length;
        
        messages.add (id, formatDescription (text, pos, length).toUtf8().data(), hint.str());

        pos += length;
    }
}

void CSMTools::Search::searchRecordStateCell (const CSMWorld::IdTableBase *model,
    const QModelIndex& index, const CSMWorld::UniversalId& id, CSMDoc::Messages& messages) const
{
    int data = model->data (index).toInt();

    if (data==mValue)
    {
        std::vector<std::string> states =
            CSMWorld::Columns::getEnums (CSMWorld::Columns::ColumnId_Modification);
    
        std::ostringstream message;
        message << states.at (data);

        std::ostringstream hint;
        hint << "r: " << model->getColumnId (index.column());
        
        messages.add (id, message.str(), hint.str());
    }
}

QString CSMTools::Search::formatDescription (const QString& description, int pos, int length) const
{
    int padding = 10; ///< \todo make this configurable

    if (pos<padding)
    {
        length += pos;
        pos = 0;
    }
    else
    {
        pos -= padding;
        length += padding;
    }

    length += padding;
    
    QString text = description.mid (pos, length);

    // compensate for Windows nonsense
    text.remove ('\r');

    // improve layout for single line display
    text.replace ("\n", "<CR>");
    text.replace ('\t', ' ');
    
    return text;    
}

CSMTools::Search::Search() : mType (Type_None) {}

CSMTools::Search::Search (Type type, const std::string& value)
: mType (type), mText (value)
{
    if (type!=Type_Text && type!=Type_Id)
        throw std::logic_error ("Invalid search parameter (string)");
}

CSMTools::Search::Search (Type type, const QRegExp& value)
: mType (type), mRegExp (value)
{
    if (type!=Type_TextRegEx && type!=Type_IdRegEx)
        throw std::logic_error ("Invalid search parameter (RegExp)");
}

CSMTools::Search::Search (Type type, int value)
: mType (type), mValue (value)
{
    if (type!=Type_RecordState)
        throw std::logic_error ("invalid search parameter (int)");
}

void CSMTools::Search::configure (const CSMWorld::IdTableBase *model)
{
    mColumns.clear();

    int columns = model->columnCount();

    for (int i=0; i<columns; ++i)
    {
        CSMWorld::ColumnBase::Display display = static_cast<CSMWorld::ColumnBase::Display> (
            model->headerData (
            i,  Qt::Horizontal, static_cast<int> (CSMWorld::ColumnBase::Role_Display)).toInt());

        bool consider = false;

        switch (mType)
        {
            case Type_Text:
            case Type_TextRegEx:

                if (CSMWorld::ColumnBase::isText (display) ||
                    CSMWorld::ColumnBase::isScript (display))
                {
                    consider = true;
                }

                break;
                
            case Type_Id:
            case Type_IdRegEx:

                if (CSMWorld::ColumnBase::isId (display) ||
                    CSMWorld::ColumnBase::isScript (display))
                {
                    consider = true;
                }

                break;
            
            case Type_RecordState:

                if (display==CSMWorld::ColumnBase::Display_RecordState)
                    consider = true;

                break;

            case Type_None:

                break;
        }

        if (consider)
            mColumns.insert (i);
    }

    mIdColumn = model->findColumnIndex (CSMWorld::Columns::ColumnId_Id);
    mTypeColumn = model->findColumnIndex (CSMWorld::Columns::ColumnId_RecordType);
}

void CSMTools::Search::searchRow (const CSMWorld::IdTableBase *model, int row,
    CSMDoc::Messages& messages) const
{
    for (std::set<int>::const_iterator iter (mColumns.begin()); iter!=mColumns.end(); ++iter)
    {
        QModelIndex index = model->index (row, *iter);

        CSMWorld::UniversalId::Type type = static_cast<CSMWorld::UniversalId::Type> (
            model->data (model->index (row, mTypeColumn)).toInt());
        
        CSMWorld::UniversalId id (
            type, model->data (model->index (row, mIdColumn)).toString().toUtf8().data());
        
        switch (mType)
        {
            case Type_Text:
            case Type_Id:

                searchTextCell (model, index, id, messages);
                break;
            
            case Type_TextRegEx:
            case Type_IdRegEx:

                searchRegExCell (model, index, id, messages);
                break;
            
            case Type_RecordState:

                searchRecordStateCell (model, index, id, messages);
                break;

            case Type_None:

                break;
        }
    }
}
