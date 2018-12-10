/*-------------------------------------------------------------------------
 *
 * foreign.h
 *	  support for foreign-data wrappers, servers and user mappings.
 *
 *
 * Portions Copyright (c) 1996-2014, PostgreSQL Global Development Group
 *
 * src/include/foreign/foreign.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef FOREIGN_H
#define FOREIGN_H

#include "nodes/parsenodes.h"


/* Helper for obtaining username for user mapping */
#define MappingUserName(userid) \
	(OidIsValid(userid) ? GetUserNameFromId(userid) : "public")


/*
 * Generic option types for validation.
 * NB! Thes are treated as flags, so use only powers of two here.
 */
typedef enum
{
	ServerOpt = 1,				/* options applicable to SERVER */
	UserMappingOpt = 2,			/* options for USER MAPPING */
	FdwOpt = 4					/* options for FOREIGN DATA WRAPPER */
} GenericOptionFlags;

typedef struct ForeignDataWrapper
{
	Oid			fdwid;			/* FDW Oid */
	Oid			owner;			/* FDW owner user Oid */
	char	   *fdwname;		/* Name of the FDW */
	Oid			fdwhandler;		/* Oid of handler function, or 0 */
	Oid			fdwvalidator;	/* Oid of validator function, or 0 */
	List	   *options;		/* fdwoptions as DefElem list */
} ForeignDataWrapper;

typedef struct ForeignServer
{
	Oid			serverid;		/* server Oid */
	Oid			fdwid;			/* foreign-data wrapper */
	Oid			owner;			/* server owner user Oid */
	char	   *servername;		/* name of the server */
	char	   *servertype;		/* server type, optional */
	char	   *serverversion;	/* server version, optional */
	List	   *options;		/* srvoptions as DefElem list */
} ForeignServer;

typedef struct UserMapping
{
	Oid			userid;			/* local user Oid */
	Oid			serverid;		/* server Oid */
	List	   *options;		/* useoptions as DefElem list */
} UserMapping;

typedef struct ForeignTable
{
	Oid			relid;			/* relation Oid */
	Oid			serverid;		/* server Oid */
	List	   *options;		/* ftoptions as DefElem list */
} ForeignTable;

/*
 * 以下是各种方便的FDW工具函数
 */

/**
 * 通过serverOID, 获取ForeignServer对象
 */
extern ForeignServer *GetForeignServer(Oid serverid);

/**
 * 通过Foreign Server的名称, 获取Foreign Server对象.
 * 如果missing_ok == true, 根据指定的Foreign Server名称, 没有找到对应的Foreign Server对象, 返回NULL,
 * 否则报错
 */
extern ForeignServer *GetForeignServerByName(const char *name, bool missing_ok);

/**
 * 通过用户Oid和ServerOID, 返回UserMapping对象
 */
extern UserMapping *GetUserMapping(Oid userid, Oid serverid);

/**
 * 通过fdw的OID, 获取FDW
 */
extern ForeignDataWrapper *GetForeignDataWrapper(Oid fdwid);

/**
 *  根据FDW的名称, 返回ForeignDataWrapper对象
 *  如果missing_ok == true, 根据指定的FDW名称, 没有找到对应的FDW对象, 返回NULL,
 *  否则报错
 */
extern ForeignDataWrapper *GetForeignDataWrapperByName(const char *name,
							bool missing_ok);

/**
 * 通过Foreign表的OID, 返回ForeignTable对象
 */
extern ForeignTable *GetForeignTable(Oid relid);

/**
 * 返回Foreign表的某个字段的所有选项值
 * 返回的是DelElem组成的list, 如果对应的字段没有option, 返回NIL
 */
extern List *GetForeignColumnOptions(Oid relid, AttrNumber attnum);

extern Oid	get_foreign_data_wrapper_oid(const char *fdwname, bool missing_ok);
extern Oid	get_foreign_server_oid(const char *servername, bool missing_ok);

#endif   /* FOREIGN_H */
