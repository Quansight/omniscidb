/**
 * @file		Analyzer.cpp
 * @author	Wei Hong <wei@map-d.com>
 * @brief		Analyzer functions
 * 
 * Copyright (c) 2014 MapD Technologies, Inc.  All rights reserved.
 **/

#include <algorithm>
#include <stdexcept>
#include "../Catalog/Catalog.h"
#include "Analyzer.h"

namespace Analyzer {

	Constant::~Constant() 
	{
		if (IS_STRING(type_info.type) && !is_null)
			delete (char*)constval.pointerval;
	}

	Subquery::~Subquery() {
		delete parsetree;
		/*
		if (plan != nullptr)
			delete plan;
			*/
	}

	InValues::~InValues() {
		delete arg;
		for (auto p : *value_list)
			delete p;
		delete value_list;
	}

	RangeTblEntry::~RangeTblEntry() {
		if (view_query != nullptr)
			delete view_query;
	}

	Query::~Query() {
		for (auto p : targetlist)
			delete p;
		for (auto p : rangetable)
			delete p;
		if (where_predicate != nullptr)
			delete where_predicate;
		if (group_by != nullptr) {
			for (auto p : *group_by)
				delete p;
			delete group_by;
		}
		if (having_predicate != nullptr)
			delete having_predicate;
		if (order_by != nullptr) {
			delete order_by;
		}
		if (next_query != nullptr)
			delete next_query;
	}

	Expr *
	ColumnVar::deep_copy() const
	{
		return new ColumnVar(type_info, table_id, column_id, rte_idx);
	}

	Expr *
	Var::deep_copy() const
	{
		return new Var(type_info, table_id, column_id, is_inner, varno);
	}

	Expr *
	Constant::deep_copy() const
	{
		Datum d = constval;
		if (IS_STRING(type_info.type) && !is_null) {
			char *str = (char*)constval.pointerval;
			char *new_str = new char[strlen(str) + 1];
			strcpy(new_str, str);
			d.pointerval = (void*)new_str;
		}
		return new Constant(type_info, is_null, d);
	}

	Expr *
	UOper::deep_copy() const
	{
		return new UOper(type_info, optype, operand->deep_copy());
	}
	
	Expr *
	BinOper::deep_copy() const
	{
		return new BinOper(type_info, optype, qualifier, left_operand->deep_copy(), right_operand->deep_copy());
	}

	Expr *
	Subquery::deep_copy() const
	{
		// not supported yet.
		assert(false);
		return nullptr;
	}

	Expr *
	InValues::deep_copy() const
	{
		std::list<Expr*> *new_value_list = new std::list<Expr*>();
		for (auto p : *value_list)
			new_value_list->push_back(p->deep_copy());
		return new InValues(arg->deep_copy(), new_value_list);
	}

	Expr *
	LikeExpr::deep_copy() const
	{
		return new LikeExpr(arg->deep_copy(), like_expr->deep_copy(), escape_expr == nullptr?nullptr:escape_expr->deep_copy());
	}

	Expr *
	AggExpr::deep_copy() const
	{
		return new AggExpr(type_info, aggtype, arg->deep_copy(), is_distinct, agg_idx);
	}

	SQLTypeInfo
	BinOper::analyze_type_info(SQLOps op, const SQLTypeInfo &left_type, const SQLTypeInfo &right_type, SQLTypeInfo *new_left_type, SQLTypeInfo *new_right_type)
	{
		SQLTypeInfo result_type;
		SQLTypeInfo common_type;
		*new_left_type = left_type;
		*new_right_type = right_type;
		if (IS_LOGIC(op)) {
			if (left_type.type != kBOOLEAN || right_type.type != kBOOLEAN)
				throw std::runtime_error("non-boolean operands cannot be used in logic operations.");
			result_type.type = kBOOLEAN;
		} else if (IS_COMPARISON(op)) {
			if ((IS_STRING(left_type.type) && !IS_STRING(right_type.type))
					||
					(!IS_STRING(left_type.type) && IS_STRING(right_type.type)))
				throw std::runtime_error("cannot compare between string and non-string types.");
			if ((IS_NUMBER(left_type.type) && !IS_NUMBER(right_type.type))
					||
					(!IS_NUMBER(left_type.type) && IS_NUMBER(right_type.type)))
				throw std::runtime_error("cannot compare between numeric and non-numeric types.");
			if (IS_NUMBER(left_type.type) && IS_NUMBER(right_type.type)) {
				common_type = common_numeric_type(left_type, right_type);
				*new_left_type = common_type;
				*new_right_type = common_type;
			}
			result_type.type = kBOOLEAN;
		} else if (IS_ARITHMETIC(op)) {
			if (!IS_NUMBER(left_type.type) || !IS_NUMBER(right_type.type))
				throw std::runtime_error("non-numeric operands in arithmetic operations.");
			common_type = common_numeric_type(left_type, right_type);
			*new_left_type = common_type;
			*new_right_type = common_type;
			result_type = common_type;
		} else {
			throw std::runtime_error("invalid binary operator type.");
		}
		return result_type;
	}

	SQLTypeInfo
	BinOper::common_numeric_type(const SQLTypeInfo &type1, const SQLTypeInfo &type2)
	{
		SQLTypeInfo common_type;
		assert(IS_NUMBER(type1.type) && IS_NUMBER(type2.type));
		if (type1.type == type2.type) {
			common_type.type = type1.type;
			common_type.dimension = std::max(type1.dimension, type2.dimension);
			common_type.scale = std::max(type1.scale, type2.scale);
			return common_type;
		}
		switch (type1.type) {
			case kSMALLINT:
				switch (type2.type) {
				case kINT:
					common_type.type = kINT;
					break;
				case kBIGINT:
					common_type.type = kBIGINT;
					break;
				case kFLOAT:
					common_type.type = kFLOAT;
					break;
				case kDOUBLE:
					common_type.type = kDOUBLE;
					break;
				case kNUMERIC:
				case kDECIMAL:
					common_type.type = kNUMERIC;
					common_type.dimension = std::max(5+type2.scale, type2.dimension);
					common_type.scale = type2.scale;
					break;
				default:
					assert(false);
				}
				break;
			case kINT:
				switch (type2.type) {
					case kSMALLINT:
						common_type.type = kINT;
						break;
					case kBIGINT:
						common_type.type = kBIGINT;
						break;
					case kFLOAT:
						common_type.type = kFLOAT;
						break;
					case kDOUBLE:
						common_type.type = kDOUBLE;
						break;
					case kNUMERIC:
					case kDECIMAL:
						common_type.type = kNUMERIC;
						common_type.dimension = std::max(std::min(19, 10+type2.scale), type2.dimension);
						common_type.scale = type2.scale;
						break;
					default:
						assert(false);
				}
				break;
			case kBIGINT:
				switch (type2.type) {
					case kSMALLINT:
						common_type.type = kBIGINT;
						break;
					case kINT:
						common_type.type = kBIGINT;
						break;
					case kFLOAT:
						common_type.type = kDOUBLE;
						break;
					case kDOUBLE:
						common_type.type = kDOUBLE;
						break;
					case kNUMERIC:
					case kDECIMAL:
						common_type.type = kNUMERIC;
						common_type.dimension = 19; // maximum precision of BIGINT
						common_type.scale = type2.scale;
						break;
					default:
						assert(false);
				}
				break;
			case kFLOAT:
				switch (type2.type) {
					case kSMALLINT:
						common_type.type = kFLOAT;
						break;
					case kINT:
						common_type.type = kFLOAT;
						break;
					case kBIGINT:
						common_type.type = kDOUBLE;
						break;
					case kDOUBLE:
						common_type.type = kDOUBLE;
						break;
					case kNUMERIC:
					case kDECIMAL:
						common_type.type = kDOUBLE;
						break;
					default:
						assert(false);
				}
				break;
			case kDOUBLE:
				switch (type2.type) {
					case kSMALLINT:
					case kINT:
					case kBIGINT:
					case kFLOAT:
					case kNUMERIC:
					case kDECIMAL:
						common_type.type = kDOUBLE;
						break;
					default:
						assert(false);
				}
				break;
			case kNUMERIC:
			case kDECIMAL:
				switch (type2.type) {
					case kSMALLINT:
						common_type.type = kNUMERIC;
						common_type.dimension = std::max(5+type2.scale, type2.dimension);
						common_type.scale = type2.scale;
						break;
					case kINT:
						common_type.type = kNUMERIC;
						common_type.dimension = std::max(std::min(19, 10+type2.scale), type2.dimension);
						common_type.scale = type2.scale;
						break;
					case kBIGINT:
						common_type.type = kNUMERIC;
						common_type.dimension = 19; // maximum precision of BIGINT
						common_type.scale = type2.scale;
						break;
					case kFLOAT:
						common_type.type = kDOUBLE; // promote to DOUBLE
						break;
					case kDOUBLE:
						common_type.type = kDOUBLE;
						break;
					case kNUMERIC:
					case kDECIMAL:
						common_type.type = kNUMERIC;
						common_type.scale = std::max(type1.scale, type2.scale);
						common_type.dimension = std::max(type1.dimension - type1.scale, type2.dimension - type2.scale) + common_type.scale;
						break;
					default:
						assert(false);
				}
				break;
				default:
					assert(false);
		}
		return common_type;
	}

	Expr *
	Expr::add_cast(const SQLTypeInfo &new_type_info)
	{
		if (new_type_info == type_info)
			return this;
		// @TODO check CASTability between types
		return new UOper(new_type_info, kCAST, this);
	}

	void
	Constant::cast_number(const SQLTypeInfo &new_type_info)
	{
		switch (type_info.type) {
			case kINT:
				switch (new_type_info.type) {
					case kINT:
						break;
					case kSMALLINT:
						constval.smallintval = (int16_t)constval.intval;
						break;
					case kBIGINT:
						constval.bigintval = (int64_t)constval.intval;
						break;
					case kDOUBLE:
						constval.doubleval = (double)constval.intval;
						break;
					case kFLOAT:
						constval.floatval = (float)constval.intval;
						break;
					case kNUMERIC:
					case kDECIMAL:
						constval.bigintval = (int64_t)constval.intval;
						for (int i = 0; i < new_type_info.scale; i++)
							constval.bigintval *= 10;
					default:
						assert(false);
				}
				break;
			case kSMALLINT:
				switch (new_type_info.type) {
					case kINT:
						constval.intval = (int32_t)constval.smallintval;
						break;
					case kSMALLINT:
						break;
					case kBIGINT:
						constval.bigintval = (int64_t)constval.smallintval;
						break;
					case kDOUBLE:
						constval.doubleval = (double)constval.smallintval;
						break;
					case kFLOAT:
						constval.floatval = (float)constval.smallintval;
						break;
					case kNUMERIC:
					case kDECIMAL:
						constval.bigintval = (int64_t)constval.smallintval;
						for (int i = 0; i < new_type_info.scale; i++)
							constval.bigintval *= 10;
					default:
						assert(false);
				}
				break;
			case kBIGINT:
				switch (new_type_info.type) {
					case kINT:
						constval.intval = (int32_t)constval.bigintval;
						break;
					case kSMALLINT:
						constval.smallintval = (int16_t)constval.bigintval;
						break;
					case kBIGINT:
						break;
					case kDOUBLE:
						constval.doubleval = (double)constval.bigintval;
						break;
					case kFLOAT:
						constval.floatval = (float)constval.bigintval;
						break;
					case kNUMERIC:
					case kDECIMAL:
						for (int i = 0; i < new_type_info.scale; i++)
							constval.bigintval *= 10;
					default:
						assert(false);
				}
				break;
			case kDOUBLE:
				switch (new_type_info.type) {
					case kINT:
						constval.intval = (int32_t)constval.doubleval;
						break;
					case kSMALLINT:
						constval.smallintval = (int16_t)constval.doubleval;
						break;
					case kBIGINT:
						constval.bigintval = (int64_t)constval.doubleval;
						break;
					case kDOUBLE:
						break;
					case kFLOAT:
						constval.floatval = (float)constval.doubleval;
						break;
					case kNUMERIC:
					case kDECIMAL:
						for (int i = 0; i < new_type_info.scale; i++)
							constval.doubleval *= 10;
						constval.bigintval = (int64_t)constval.doubleval;
					default:
						assert(false);
				}
				break;
			case kFLOAT:
				switch (new_type_info.type) {
					case kINT:
						constval.intval = (int32_t)constval.floatval;
						break;
					case kSMALLINT:
						constval.smallintval = (int16_t)constval.floatval;
						break;
					case kBIGINT:
						constval.bigintval = (int64_t)constval.floatval;
						break;
					case kDOUBLE:
						constval.doubleval = (double)constval.floatval;
						break;
					case kFLOAT:
						break;
					case kNUMERIC:
					case kDECIMAL:
						for (int i = 0; i < new_type_info.scale; i++)
							constval.floatval *= 10;
						constval.bigintval = (int64_t)constval.floatval;
					default:
						assert(false);
				}
				break;
			case kNUMERIC:
			case kDECIMAL:
				switch (new_type_info.type) {
					case kINT:
						for (int i = 0; i < type_info.scale; i++)
							constval.bigintval /= 10;
						constval.intval = (int32_t)constval.bigintval;
						break;
					case kSMALLINT:
						for (int i = 0; i < type_info.scale; i++)
							constval.bigintval /= 10;
						constval.smallintval = (int16_t)constval.bigintval;
						break;
					case kBIGINT:
						for (int i = 0; i < type_info.scale; i++)
							constval.bigintval /= 10;
						break;
					case kDOUBLE:
						constval.doubleval = (double)constval.bigintval;
						for (int i = 0; i < type_info.scale; i++)
							constval.doubleval /= 10;
						break;
					case kFLOAT:
						constval.floatval = (float)constval.bigintval;
						for (int i = 0; i < type_info.scale; i++)
							constval.floatval /= 10;
						break;
					case kNUMERIC:
					case kDECIMAL:
						if (new_type_info.scale > type_info.scale) {
							for (int i = 0; i < new_type_info.scale - type_info.scale; i++)
								constval.bigintval *= 10;
						} else if (new_type_info.scale < type_info.scale) {
							for (int i = 0; i < type_info.scale - new_type_info.scale; i++)
								constval.bigintval /= 10;
						}
					default:
						assert(false);
				}
				break;
			default:
				assert(false);
		}
		type_info = new_type_info;
	}

	void
	Constant::cast_string(const SQLTypeInfo &new_type_info)
	{
		char *str = (char*)constval.pointerval;
		if (new_type_info.type != kTEXT && new_type_info.dimension < strlen(str)) {
			// truncate string
			char *new_str = new char[new_type_info.dimension + 1];
			strncpy(new_str, str, new_type_info.dimension);
			delete str;
			constval.pointerval = (void*)new_str;
		}
		type_info = new_type_info;
	}

	Expr *
	Constant::add_cast(const SQLTypeInfo &new_type_info)
	{
		if (is_null) {
			type_info = new_type_info;
			return this;
		}
		if (IS_NUMBER(new_type_info.type) && IS_NUMBER(type_info.type)) {
			cast_number(new_type_info);
			return this;
		} else if (IS_STRING(new_type_info.type) && IS_STRING(type_info.type)) {
			cast_string(new_type_info);
			return this;
		}
		return Expr::add_cast(new_type_info);
	}

	Expr *
	Subquery::add_cast(const SQLTypeInfo &new_type_info)
	{
		// not supported yet.
		assert(false);
		return nullptr;
	}

	void
	RangeTblEntry::add_all_column_descs(const Catalog_Namespace::Catalog &catalog)
	{
		column_descs = catalog.getAllColumnMetadataForTable(table_desc->tableId);
	}

	void
	RangeTblEntry::expand_star_in_targetlist(const Catalog_Namespace::Catalog &catalog, std::list<TargetEntry*> &tlist, int rte_idx)
	{
		column_descs = catalog.getAllColumnMetadataForTable(table_desc->tableId);
		for (auto col_desc : column_descs) {
			ColumnVar *cv = new ColumnVar(col_desc->columnType, table_desc->tableId, col_desc->columnId, rte_idx);
			TargetEntry *tle = new TargetEntry(col_desc->columnName, cv);
			tlist.push_back(tle);
		}
	}

	const ColumnDescriptor *
	RangeTblEntry::get_column_desc(const Catalog_Namespace::Catalog &catalog, const std::string &name)
	{
		for (auto cd : column_descs) {
			if (cd->columnName == name)
				return cd;
		}
		const ColumnDescriptor *cd = catalog.getMetadataForColumn(table_desc->tableId, name);
		if (cd != nullptr)
			column_descs.push_back(cd);
		return cd;
	}

	int
	Query::get_rte_idx(const std::string &name) const
	{
		int rte_idx = 0;
		for (auto rte : rangetable) {
			if (rte->get_rangevar() == name)
				return rte_idx;
			rte_idx++;
		}
		return -1;
	}

	void
	Query::add_rte(RangeTblEntry *rte)
	{
		rangetable.push_back(rte);
	}

	void
	ColumnVar::check_group_by(const std::list<Expr*> *groupby) const
	{
		if (groupby != nullptr) {
			for (auto e : *groupby) {
				ColumnVar *c = dynamic_cast<ColumnVar*>(e);
				if (table_id == c->get_table_id() && column_id == c->get_column_id())
					return;
			}
		}
		throw std::runtime_error("expressions in the SELECT or HAVING clause must be an aggregate function or an expression over GROUP BY columns.");
	}

	void
	UOper::check_group_by(const std::list<Expr*> *groupby) const
	{
		operand->check_group_by(groupby);
	}

	void
	BinOper::check_group_by(const std::list<Expr*> *groupby) const
	{
		left_operand->check_group_by(groupby);
		right_operand->check_group_by(groupby);
	}

	Expr *
	BinOper::normalize_simple_predicate(int &rte_idx) const
	{
		rte_idx = -1;
		if (!IS_COMPARISON(optype))
			return nullptr;
		if (typeid(*left_operand) == typeid(ColumnVar) && typeid(*right_operand) == typeid(Constant)) {
			ColumnVar *cv = dynamic_cast<ColumnVar*>(left_operand);
			rte_idx = cv->get_rte_idx();
			return this->deep_copy();
		} else if (typeid(*left_operand) == typeid(Constant) && typeid(*right_operand) == typeid(ColumnVar)) {
			ColumnVar *cv = dynamic_cast<ColumnVar*>(right_operand);
			rte_idx = cv->get_rte_idx();
			return new BinOper(type_info, COMMUTE_COMPARISON(optype), qualifier, right_operand->deep_copy(), left_operand->deep_copy());
		}
		return nullptr;
	}


	void
	ColumnVar::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		if (type_info.type == kBOOLEAN)
			scan_predicates.push_back(this);
	}

	void
	UOper::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		std::set<int> rte_idx_set;
		operand->collect_rte_idx(rte_idx_set);
		if(rte_idx_set.size() > 1)
			join_predicates.push_back(this);
		else if (rte_idx_set.size() == 1)
			scan_predicates.push_back(this);
		else
			const_predicates.push_back(this);
	}

	void
	BinOper::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		if (optype == kAND) {
			left_operand->group_predicates(scan_predicates, join_predicates, const_predicates);
			right_operand->group_predicates(scan_predicates, join_predicates, const_predicates);
			return;
		}
		std::set<int> rte_idx_set;
		left_operand->collect_rte_idx(rte_idx_set);
		right_operand->collect_rte_idx(rte_idx_set);
		if(rte_idx_set.size() > 1)
			join_predicates.push_back(this);
		else if (rte_idx_set.size() == 1)
			scan_predicates.push_back(this);
		else
			const_predicates.push_back(this);
	}

	void
	InValues::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		std::set<int> rte_idx_set;
		arg->collect_rte_idx(rte_idx_set);
		if(rte_idx_set.size() > 1)
			join_predicates.push_back(this);
		else if (rte_idx_set.size() == 1)
			scan_predicates.push_back(this);
		else
			const_predicates.push_back(this);
	}

	void
	LikeExpr::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		std::set<int> rte_idx_set;
		arg->collect_rte_idx(rte_idx_set);
		if(rte_idx_set.size() > 1)
			join_predicates.push_back(this);
		else if (rte_idx_set.size() == 1)
			scan_predicates.push_back(this);
		else
			const_predicates.push_back(this);
	}

	void
	AggExpr::group_predicates(std::list<const Expr*> &scan_predicates, std::list<const Expr*> &join_predicates, std::list<const Expr*> &const_predicates) const
	{
		std::set<int> rte_idx_set;
		arg->collect_rte_idx(rte_idx_set);
		if(rte_idx_set.size() > 1)
			join_predicates.push_back(this);
		else if (rte_idx_set.size() == 1)
			scan_predicates.push_back(this);
		else
			const_predicates.push_back(this);
	}

	Expr *
	ColumnVar::rewrite_with_targetlist(const std::list<TargetEntry*> &tlist) const
	{
		for (auto tle : tlist) {
			const Expr *e = tle->get_expr();
			if (typeid(*e) != typeid(AggExpr)) {
				const ColumnVar *colvar = dynamic_cast<const ColumnVar*>(e);
				if (table_id == colvar->get_table_id() && column_id == colvar->get_column_id())
					return colvar->deep_copy();
			}
		}
		throw std::runtime_error("Intern error: cannot find ColumnVar in targetlist.");
	}

	Expr *
	InValues::rewrite_with_targetlist(const std::list<TargetEntry*> &tlist) const
	{
		std::list<Expr*> *new_value_list = new std::list<Expr*>();
		for (auto v : *value_list)
			new_value_list->push_back(v->deep_copy());
		return new InValues(arg->rewrite_with_targetlist(tlist), new_value_list);
	}

	Expr *
	AggExpr::rewrite_with_targetlist(const std::list<TargetEntry*> &tlist) const
	{
		for (auto tle : tlist) {
			const Expr *e = tle->get_expr();
			if (typeid(*e) == typeid(AggExpr)) {
				const AggExpr *agg = dynamic_cast<const AggExpr*>(e);
				if (agg_idx == agg->get_agg_idx())
					return agg->deep_copy();
			}
		}
		throw std::runtime_error("Intern error: cannot find AggExpr in targetlist.");
	}

}
