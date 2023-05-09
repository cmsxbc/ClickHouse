#pragma once

#include <base/scope_guard.h>

#include <Common/Exception.h>
#include <Core/Settings.h>

#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/QueryNode.h>
#include <Analyzer/TableFunctionNode.h>
#include <Analyzer/UnionNode.h>

#include <Interpreters/Context.h>

namespace DB
{

/** Visitor that traverse query tree in depth.
  * Derived class must implement `visitImpl` method.
  * Additionally subclass can control if child need to be visited using `needChildVisit` method, by
  * default all node children are visited.
  * By default visitor traverse tree from top to bottom, if bottom to top traverse is required subclass
  * can override `shouldTraverseTopToBottom` method.
  *
  * Usage example:
  * class FunctionsVisitor : public InDepthQueryTreeVisitor<FunctionsVisitor>
  * {
  *     void visitImpl(VisitQueryTreeNodeType & query_tree_node)
  *     {
  *         if (query_tree_node->getNodeType() == QueryTreeNodeType::FUNCTION)
  *             processFunctionNode(query_tree_node);
  *     }
  * }
  */
template <typename Derived, bool const_visitor = false>
class InDepthQueryTreeVisitor
{
public:
    using VisitQueryTreeNodeType = std::conditional_t<const_visitor, const QueryTreeNodePtr, QueryTreeNodePtr>;

    /// Return true if visitor should traverse tree top to bottom, false otherwise
    bool shouldTraverseTopToBottom() const
    {
        return true;
    }

    /// Return true if visitor should visit child, false otherwise
    bool needChildVisit(VisitQueryTreeNodeType & parent [[maybe_unused]], VisitQueryTreeNodeType & child [[maybe_unused]])
    {
        return true;
    }

    void visit(VisitQueryTreeNodeType & query_tree_node)
    {
        bool traverse_top_to_bottom = getDerived().shouldTraverseTopToBottom();
        if (!traverse_top_to_bottom)
            visitChildren(query_tree_node);

        getDerived().visitImpl(query_tree_node);

        if (traverse_top_to_bottom)
            visitChildren(query_tree_node);
    }

private:
    Derived & getDerived()
    {
        return *static_cast<Derived *>(this);
    }

    const Derived & getDerived() const
    {
        return *static_cast<Derived *>(this);
    }

    void visitChildren(VisitQueryTreeNodeType & expression)
    {
        for (auto & child : expression->getChildren())
        {
            if (!child)
                continue;

            bool need_visit_child = getDerived().needChildVisit(expression, child);

            if (need_visit_child)
                visit(child);
        }
    }
};

template <typename Derived>
using ConstInDepthQueryTreeVisitor = InDepthQueryTreeVisitor<Derived, true /*const_visitor*/>;

/** Same as InDepthQueryTreeVisitor and additionally keeps track of current scope context.
  * This can be useful if your visitor has special logic that depends on current scope context.
  */
template <typename Derived, bool const_visitor = false>
class InDepthQueryTreeVisitorWithContext
{
public:
    using VisitQueryTreeNodeType = std::conditional_t<const_visitor, const QueryTreeNodePtr, QueryTreeNodePtr>;

    explicit InDepthQueryTreeVisitorWithContext(ContextPtr context, size_t initial_subquery_depth = 0)
        : current_context(std::move(context))
        , subquery_depth(initial_subquery_depth)
    {}

    /// Return true if visitor should traverse tree top to bottom, false otherwise
    bool shouldTraverseTopToBottom() const
    {
        return true;
    }

    /// Return true if visitor should visit child, false otherwise
    bool needChildVisit(VisitQueryTreeNodeType & parent [[maybe_unused]], VisitQueryTreeNodeType & child [[maybe_unused]])
    {
        return true;
    }

    const ContextPtr & getContext() const
    {
        return current_context;
    }

    const Settings & getSettings() const
    {
        return current_context->getSettingsRef();
    }

    size_t getSubqueryDepth() const
    {
        return subquery_depth;
    }

    void visit(VisitQueryTreeNodeType & query_tree_node)
    {
        auto current_scope_context_ptr = current_context;
        SCOPE_EXIT(
            current_context = std::move(current_scope_context_ptr);
            --subquery_depth;
        );

        if (auto * query_node = query_tree_node->template as<QueryNode>())
            current_context = query_node->getContext();
        else if (auto * union_node = query_tree_node->template as<UnionNode>())
            current_context = union_node->getContext();

        ++subquery_depth;

        bool traverse_top_to_bottom = getDerived().shouldTraverseTopToBottom();
        if (!traverse_top_to_bottom)
            visitChildren(query_tree_node);

        getDerived().visitImpl(query_tree_node);

        if (traverse_top_to_bottom)
            visitChildren(query_tree_node);

        getDerived().leaveImpl(query_tree_node);
    }

    void leaveImpl(VisitQueryTreeNodeType & node [[maybe_unused]])
    {}
private:
    Derived & getDerived()
    {
        return *static_cast<Derived *>(this);
    }

    const Derived & getDerived() const
    {
        return *static_cast<Derived *>(this);
    }

    void visitChildren(VisitQueryTreeNodeType & expression)
    {
        for (auto & child : expression->getChildren())
        {
            if (!child)
                continue;

            bool need_visit_child = getDerived().needChildVisit(expression, child);

            if (need_visit_child)
                visit(child);
        }
    }

    ContextPtr current_context;
    size_t subquery_depth = 0;
};

template <typename Derived>
using ConstInDepthQueryTreeVisitorWithContext = InDepthQueryTreeVisitorWithContext<Derived, true /*const_visitor*/>;

/** Visitor that use another visitor to visit node only if condition for visiting node is true.
  * For example, your visitor need to visit only query tree nodes or union nodes.
  *
  * Condition interface:
  * struct Condition
  * {
  *     bool operator()(VisitQueryTreeNodeType & node)
  *     {
  *         return shouldNestedVisitorVisitNode(node);
  *     }
  * }
  */
template <typename Visitor, typename Condition, bool const_visitor = false>
class InDepthQueryTreeConditionalVisitor : public InDepthQueryTreeVisitor<InDepthQueryTreeConditionalVisitor<Visitor, Condition, const_visitor>, const_visitor>
{
public:
    using Base = InDepthQueryTreeVisitor<InDepthQueryTreeConditionalVisitor<Visitor, Condition, const_visitor>, const_visitor>;
    using VisitQueryTreeNodeType = typename Base::VisitQueryTreeNodeType;

    explicit InDepthQueryTreeConditionalVisitor(Visitor & visitor_, Condition & condition_)
        : visitor(visitor_)
        , condition(condition_)
    {
    }

    bool shouldTraverseTopToBottom() const
    {
        return visitor.shouldTraverseTopToBottom();
    }

    void visitImpl(VisitQueryTreeNodeType & query_tree_node)
    {
        if (condition(query_tree_node))
            visitor.visit(query_tree_node);
    }

    Visitor & visitor;
    Condition & condition;
};

template <typename Visitor, typename Condition>
using ConstInDepthQueryTreeConditionalVisitor = InDepthQueryTreeConditionalVisitor<Visitor, Condition, true /*const_visitor*/>;

template <typename Impl>
class QueryTreeVisitor
{
public:
    explicit QueryTreeVisitor(ContextPtr context_)
        : current_context(std::move(context_))
    {}

    bool needApply(QueryTreeNodePtr & node)
    {
        return getImpl().needApply(node);
    }

    bool shouldSkipSubtree(QueryTreeNodePtr & parent, size_t subtree_index)
    {
        if (auto * table_function_node = parent->as<TableFunctionNode>())
        {
            const auto & unresolved_indexes = table_function_node->getUnresolvedArgumentIndexes();
            return std::find(unresolved_indexes.begin(), unresolved_indexes.end(), subtree_index) != unresolved_indexes.end();
        }
        return false;
    }

    void visit(QueryTreeNodePtr & node)
    {
        auto current_scope_context_ptr = current_context;
        SCOPE_EXIT(
            current_context = std::move(current_scope_context_ptr);
        );

        if (auto * query_node = node->template as<QueryNode>())
            current_context = query_node->getContext();
        else if (auto * union_node = node->template as<UnionNode>())
            current_context = union_node->getContext();

        if (!TOP_TO_BOTTOM)
            visitChildren(node);

        if (needApply(node))
            getImpl().apply(node);

        if (TOP_TO_BOTTOM)
            visitChildren(node);
    }

    const ContextPtr & getContext() const
    {
        return current_context;
    }

    const Settings & getSettings() const
    {
        return current_context->getSettingsRef();
    }
private:

    Impl & getImpl()
    {
        return *static_cast<Impl *>(this);
    }

    void visitChildren(QueryTreeNodePtr & node)
    {
        size_t index = 0;
        for (auto & child : node->getChildren())
        {
            if (child && !shouldSkipSubtree(node, index))
                visit(child);
            ++index;
        }
    }

    static constexpr bool TOP_TO_BOTTOM = Impl::TOP_TO_BOTTOM;

    ContextPtr current_context;
};

}
