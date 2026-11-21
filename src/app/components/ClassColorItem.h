#pragma once
#include <QString>
#include <QMetaObject>
struct  ClassColorItem
{
	Q_GADGET
public:
	ClassColorItem () = default;
	ClassColorItem(const QString& clrName,const QString& clsName,int cls):m_clrName(clrName),m_clsName(clsName),m_cls(cls){}
	Q_INVOKABLE int cls ()const { return m_cls; }
	Q_INVOKABLE QString clsName ()const { return m_clsName; }
	Q_INVOKABLE QString clrName ()const { return m_clrName; }
	void setCls(int cls_)
	{
		m_cls = cls_;
	}
	void setClsName(const QString& name)
	{
		m_clsName = name;
	}
	void setClrName(const QString & name)
	{
		m_clrName = name;
	}
	QString m_clrName;
	QString m_clsName;
	int m_cls = -1;
};