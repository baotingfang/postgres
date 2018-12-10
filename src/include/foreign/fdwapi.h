/*-------------------------------------------------------------------------
 *
 * fdwapi.h
 *	  API for foreign-data wrappers
 *
 * Copyright (c) 2010-2014, PostgreSQL Global Development Group
 *
 * src/include/foreign/fdwapi.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef FDWAPI_H
#define FDWAPI_H

#include "nodes/execnodes.h"
#include "nodes/relation.h"

/* To avoid including explain.h here, reference ExplainState thus: */
struct ExplainState;


/*
 * Callback function signatures --- see fdwhandler.sgml for more info.
 */

/**
 * 估计外部表的大小. 该函数会在对query最开始做计划时调用.
 *
 * root: 相关query的planner的全局信息
 * baserel: pleanner中与当前table相关的信息
 * foreigntableid: foreign table在pg_class中的OID
 *
 * 实现该函数最重要的是: 设置baserel->rows的值,
 * 它表示对该表扫描后的行数的估计值 (已排除过滤的行)
 * 用户也可以设置: baserel->width, 该值指定了平均的row的宽度, 可以更好的作出估计.
 */
typedef void (*GetForeignRelSize_function) (PlannerInfo *root,
														RelOptInfo *baserel,
														Oid foreigntableid);

/**
 * 创建扫描foreign表的访问路径列表(access path). 该函数会在query做plan时调用
 *
 * root: 相关query的planner的全局信息
 * baserel: pleanner中与当前table相关的信息
 * foreigntableid: foreign table在pg_class中的OID
 *
 * 该函数必须生成至少1个访问路径, 访问路径对应的是ForeignPath节点. 对于每一个访问路径,
 * 都需要调用add_path()方法, 将访问路径节点添加到baserel->pathlist.
 *
 * 推荐使用:create_foreignscan_path()函数创建ForeignPath节点集合, 该函数会创建多个
 * 访问路径, 例如, 如果某个访问路径节点中含有有效的pathkeys, 表明它是预排序的结果集.
 * 每个访问路径都必须要包含cost估计, 同时可以包含任意个FDW的私有信息
 */
typedef void (*GetForeignPaths_function) (PlannerInfo *root,
													  RelOptInfo *baserel,
													  Oid foreigntableid);
/**
 * 根据已选择的foreign访问路径, 创建ForeignScan计划节点. 该函数会在做query plane的
 * 最后阶段被调用
 *
 * root: 相关query的planner的全局信息
 * baserel: pleanner中与当前table相关的信息
 * foreigntableid: foreign table在pg_class中的OID
 * best_path: 选择的ForeignPath, 由GetForeignPaths()函数生成.
 * tlist: 由plan节点提供的target list
 * scan_clauses: plan节点的约束条件
 *
 * 该函数必须要创建并返回一个ForeignScan计划节点,
 * 推荐使用: make_foreignscan()函数生成ForeignScan节点
 */
typedef ForeignScan *(*GetForeignPlan_function) (PlannerInfo *root,
														 RelOptInfo *baserel,
														  Oid foreigntableid,
													  ForeignPath *best_path,
															 List *tlist,
														 List *scan_clauses);

/**
 * 开始执行foreign scan操作. 该函数会在执行器启动阶段调用.
 *
 * 在开始scan之前的任何操作, 都可以添加到该函数中. 调用该函数中, 还没有真正开始scan.
 * 只有在完成对IterateFoeignScan()的第一次调用后, 才开始真正的scan操作.
 *
 * node:	该参数已经创建, 但是fdw_state字段为NULL. 可以通过该参数访问到将要扫描的表的信息.
 * 			特别是底层的ForeignScan计划节点, 它包含了很多与FDW相关的私有信息, ForeignScan
 * 			计划节点, 由GetForeignPlan()函数生成
 * eflags:	指定了计划节点在执行器中的操作模式
 *
 * 当 (eflags & EXEC_FLAG_EXPLAIN_ONLY) == true, 该函数不应该执行任何外部可见的操作.
 * 而应该只做一些最小化, 最必要的操作, 使得在调用ExplainForeignScan()和EndForeignScan()时,
 * 节点的状态有效.
 */
typedef void (*BeginForeignScan_function) (ForeignScanState *node,
													   int eflags);

/**
 * 从外部源获取1行记录, 将其返回到TupleTableSlot中 (node->ss->ss_ScanTupleSlot)
 * 如果没有行返回, 可以返回NULL. tuple table slot这个基础设置, 可以保存物理的tuple,
 * 也可以保存虚拟的tuple. 在大多数使用场景中, 会保存虚拟tuple, 这样可以提高性能.
 *
 * 注意: 该函数调用会切换memory context, 并且该memory context存在的时间非常短暂.每次
 * 调用该函数, 该memory context都会被重置. 如果用户希望在FDW中使用"长期"的memory context,
 * 则应该在BeginForeignScan()函数中设置, 并且保存在node->ss->ps->state->es_query_cxt中.
 *
 * 返回的行中的字段, 必须与scan的foreign表的column签名一致. 如果选择优化取出不需要的列,
 * 则应该在这些位置上插入NULL
 *
 * pg执行器并不关心返回的行是否违反了在Foreign表的列上定义的NOT NULL的约束.
 * 	--- 但是planner会对这些约束进行检查---
 * 	pg执行器可能会对query进行不正确的优化, 比如那些不应该出现NULL的字段, 设置为了NULL字段,
 * 	如果发生这种情况, 将会引发类型不匹配的错误.
 */
typedef TupleTableSlot *(*IterateForeignScan_function) (ForeignScanState *node);

/**
 * 重新开始扫描, 因为与扫描相关的任何参数都可能发生改变, 因此多次执行, 可能返回的结果不相同
 */
typedef void (*ReScanForeignScan_function) (ForeignScanState *node);

/**
 * 结束扫描, 释放各种资源. 释放palloc的内存并不是非常重要, 重要的释放打开的文件和打开的各种连接.
 */
typedef void (*EndForeignScan_function) (ForeignScanState *node);

typedef void (*AddForeignUpdateTargets_function) (Query *parsetree,
												   RangeTblEntry *target_rte,
												   Relation target_relation);

typedef List *(*PlanForeignModify_function) (PlannerInfo *root,
														 ModifyTable *plan,
														 Index resultRelation,
														 int subplan_index);

typedef void (*BeginForeignModify_function) (ModifyTableState *mtstate,
														 ResultRelInfo *rinfo,
														 List *fdw_private,
														 int subplan_index,
														 int eflags);

typedef TupleTableSlot *(*ExecForeignInsert_function) (EState *estate,
														ResultRelInfo *rinfo,
														TupleTableSlot *slot,
												   TupleTableSlot *planSlot);

typedef TupleTableSlot *(*ExecForeignUpdate_function) (EState *estate,
														ResultRelInfo *rinfo,
														TupleTableSlot *slot,
												   TupleTableSlot *planSlot);

typedef TupleTableSlot *(*ExecForeignDelete_function) (EState *estate,
														ResultRelInfo *rinfo,
														TupleTableSlot *slot,
												   TupleTableSlot *planSlot);

typedef void (*EndForeignModify_function) (EState *estate,
													   ResultRelInfo *rinfo);

typedef int (*IsForeignRelUpdatable_function) (Relation rel);

typedef void (*ExplainForeignScan_function) (ForeignScanState *node,
													struct ExplainState *es);

typedef void (*ExplainForeignModify_function) (ModifyTableState *mtstate,
														ResultRelInfo *rinfo,
														   List *fdw_private,
														   int subplan_index,
													struct ExplainState *es);

typedef int (*AcquireSampleRowsFunc) (Relation relation, int elevel,
											   HeapTuple *rows, int targrows,
												  double *totalrows,
												  double *totaldeadrows);

typedef bool (*AnalyzeForeignTable_function) (Relation relation,
												 AcquireSampleRowsFunc *func,
													BlockNumber *totalpages);

/**
 * FDW的查询计划(Query Planning)
 *
 * FDW的回调函数中的: GetForeignRelSize, GetForeignPaths, GetForeignPlan,
 * PlanForeignModify, 会在pg planner合适的位置被调用.
 *
 * typedef void (*GetForeignRelSize_function) (PlannerInfo *root,
														RelOptInfo *baserel,
														Oid foreigntableid);
 * 例如, 这里的root和baserel参数, 就提供了很多有用的信息.
 *
 * baserel->baserestrictinfo
 *
 * 		它包括了约束条件信息(Where从句), 该信息可以用于过滤操作. 这对push-down操作, 有很重要的意义,
 * 		但同时, 这又不是必须的, 因为即使返回了所有的结果, 执行器也会做再次的检查, 过滤掉应该过滤的行.
 *
 * baserel->reltargetlist
 *
 * 		它包括了需要从远端抓取的字段, 注意, 它只包括了以ForeignScan计划节点指定的字段, 并不包括
 * 		在条件从句中使用的字段, 但又没有在查询中输出的字段.
 *
 * 在FDW的各种回调函数中, 可以由许多私有的字段信息. 这些信息需要使用palloc进行内存分配,
 * 在FDW结束时, 也需要进行回收.
 *
 * 这些私有信息保存在: baserel->fdw_private, 它是void*类型, 它指向的内存区域, 保存了与该foreign表相关
 * 的信息. pg的planner是不会碰这些信息的:
 *
 * 		只有在创建baserel节点是, pg的planner会将baserel->fdw_private设置为NULL.
 *
 * baserel->fdw_private 用途:
 *
 * 		在GetForeignRelSize ===> GetForeignPaths ==> GetFoeignPlan之间传递信息.
 * 		这样, 就可以避免重复计算.
 *
 * 		GetFoeignPaths()可以通过访问私有信息, 识别出不同访问路径的含义. baserel->fdw_private声明为
 * 		List*, 其实, 可以在其中保存任何想要保存的信息. 因为pg的planner是不会碰这里的信息的.
 * 		最佳实践: 私有信息最好是支持nodeToString()的函数, 这样可以在debug时, 输出更加友好的显示结果.
 *
 * 		GetForeignPlan()可以通过fdw_private字段, 得到已选择的ForeignPath节点, 而后生成fdw_exprs,
 * 		和fdw_private的list, 并且将它们赋值给ForeignScan计划节点, 计划节点可以在之后的运行期间访问.
 * 		这两个list必须支持copyObject()方法实现copy功能.
 *
 * 			fdw_private list: 没有其他多余的限制, 也不会被backend做任何解释
 * 			fdw_exprs list: 如果不为NULL, 则期望包含expression trees, 会在运行时被执行. 这些树将会
 * 							在planner的postprocessing中执行
 *
 * typedef ForeignScan *(*GetForeignPlan_function) (PlannerInfo *root,
														 RelOptInfo *baserel,
														  Oid foreigntableid,
													  ForeignPath *best_path,
															 List *tlist,
														 List *scan_clauses);
 *		通常, 传入的target list会被原样复制到plan节点. 传入的scan_clauses list包含与
 *		baserel->baserestrictinfo相同的从句信息, 只是这些从句可能被重新排序, 从而可以提高执行效率.
 *
 *		在一些简单场景下, FDW只是从scan_clauses list中移除了RestrictInfo节点, (通过extract_actual_clauses函数实现),
 *		然后, 将所有的从句发到plan节点的qual list中, 这就意味着, 所有的条件从句由执行器在运行时进行检查.
 *
 *		一些比较复杂的FDW, 可以在自己内部检查某些从句, 在这种情况下, 这些从句会从节点的qual list移除, 因此
 *		执行器不会再重新检查这些约束条件.
 *
 *		例如, FDW可以识别foreign_variable=sub_expression形式, 从而识别出某些限制条件. 可以通过在本地评估sub_expression的值,
 *		以决定是否可以在远程服务器上执行. 这样从句的识别操作只能发生在GetForeignPaths函数期间, 因为它会影响到path的成本估计. path
 *		的fdw_private字段可能是一个指向约束条件的RestrictInfo节点. GetForeignPlan函数会从scan_clauses list中移除该从句, 并且
 *		将sub_expression添加到fdw_expres list中.也有可能将控制信息放入到fdw_private字段中, 以便告诉执行器在运行的时候应该做什么.
 *		发送给远程服务器的query, 会自动添加类似WHERE foreign_variable = $1, 这里的参数信息会在运行时从fdw_expres表达式树中获取.
 *
 *		FDW应该根据表的限制条件, 至少创建1条访问路径.在连表查询中, 也许会选择依赖join从句, 创建的访问路径.
 *		例如: foreign_variable = local_variable, 这样的从句不会在baserel->baserestrictinfo中被找到, 但是应该出现表的join list中.
 *		使用这种从句访问路径称为参数化路径.
 */

/*
 * FdwRoutine is the struct returned by a foreign-data wrapper's handler
 * function.  It provides pointers to the callback functions needed by the
 * planner and executor.
 *
 * More function pointers are likely to be added in the future.  Therefore
 * it's recommended that the handler initialize the struct with
 * makeNode(FdwRoutine) so that all fields are set to NULL.  This will
 * ensure that no fields are accidentally left undefined.
 */
typedef struct FdwRoutine
{
	NodeTag		type;

	/* Functions for scanning foreign tables */
	GetForeignRelSize_function GetForeignRelSize;
	GetForeignPaths_function GetForeignPaths;
	GetForeignPlan_function GetForeignPlan;
	BeginForeignScan_function BeginForeignScan;
	IterateForeignScan_function IterateForeignScan;
	ReScanForeignScan_function ReScanForeignScan;
	EndForeignScan_function EndForeignScan;

	/*
	 * Remaining functions are optional.  Set the pointer to NULL for any that
	 * are not provided.
	 */

	/* Functions for updating foreign tables */
	AddForeignUpdateTargets_function AddForeignUpdateTargets;
	PlanForeignModify_function PlanForeignModify;
	BeginForeignModify_function BeginForeignModify;
	ExecForeignInsert_function ExecForeignInsert;
	ExecForeignUpdate_function ExecForeignUpdate;
	ExecForeignDelete_function ExecForeignDelete;
	EndForeignModify_function EndForeignModify;
	IsForeignRelUpdatable_function IsForeignRelUpdatable;

	/* Support functions for EXPLAIN */
	ExplainForeignScan_function ExplainForeignScan;
	ExplainForeignModify_function ExplainForeignModify;

	/* Support functions for ANALYZE */
	AnalyzeForeignTable_function AnalyzeForeignTable;
} FdwRoutine;


/* Functions in foreign/foreign.c */
extern FdwRoutine *GetFdwRoutine(Oid fdwhandler);
extern FdwRoutine *GetFdwRoutineByRelId(Oid relid);
extern FdwRoutine *GetFdwRoutineForRelation(Relation relation, bool makecopy);

#endif   /* FDWAPI_H */
