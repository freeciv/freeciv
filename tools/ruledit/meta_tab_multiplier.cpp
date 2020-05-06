/****************************************************************************
** Meta object code from reading C++ file 'tab_multiplier.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.14.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "tab_multiplier.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'tab_multiplier.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.14.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_tab_multiplier_t {
    QByteArrayData data[9];
    char stringdata0[99];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_tab_multiplier_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_tab_multiplier_t qt_meta_stringdata_tab_multiplier = {
    {
QT_MOC_LITERAL(0, 0, 14), // "tab_multiplier"
QT_MOC_LITERAL(1, 15, 10), // "name_given"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 17), // "select_multiplier"
QT_MOC_LITERAL(4, 45, 7), // "add_now"
QT_MOC_LITERAL(5, 53, 10), // "delete_now"
QT_MOC_LITERAL(6, 64, 16), // "same_name_toggle"
QT_MOC_LITERAL(7, 81, 7), // "checked"
QT_MOC_LITERAL(8, 89, 9) // "edit_reqs"

    },
    "tab_multiplier\0name_given\0\0select_multiplier\0"
    "add_now\0delete_now\0same_name_toggle\0"
    "checked\0edit_reqs"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_tab_multiplier[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       6,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   44,    2, 0x08 /* Private */,
       3,    0,   45,    2, 0x08 /* Private */,
       4,    0,   46,    2, 0x08 /* Private */,
       5,    0,   47,    2, 0x08 /* Private */,
       6,    1,   48,    2, 0x08 /* Private */,
       8,    0,   51,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    7,
    QMetaType::Void,

       0        // eod
};

void tab_multiplier::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<tab_multiplier *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->name_given(); break;
        case 1: _t->select_multiplier(); break;
        case 2: _t->add_now(); break;
        case 3: _t->delete_now(); break;
        case 4: _t->same_name_toggle((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 5: _t->edit_reqs(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject tab_multiplier::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_meta_stringdata_tab_multiplier.data,
    qt_meta_data_tab_multiplier,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *tab_multiplier::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *tab_multiplier::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_tab_multiplier.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int tab_multiplier::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 6;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
