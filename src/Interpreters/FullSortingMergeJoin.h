#pragma once

#include <Interpreters/IJoin.h>
#include <Interpreters/TableJoin.h>
#include <DataTypes/DataTypeNullable.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <Poco/Logger.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int TYPE_MISMATCH;
    extern const int NOT_IMPLEMENTED;
}

/// Dummy class, actual joining is done by MergeTransform
class FullSortingMergeJoin : public IJoin
{
public:
    explicit FullSortingMergeJoin(std::shared_ptr<TableJoin> table_join_, const Block & right_sample_block_)
        : table_join(table_join_)
        , right_sample_block(right_sample_block_)
    {
        LOG_TRACE(&Poco::Logger::get("FullSortingMergeJoin"), "Will use full sorting merge join");
    }

    const TableJoin & getTableJoin() const override { return *table_join; }

    bool addJoinedBlock(const Block & /* block */, bool /* check_limits */) override { __builtin_unreachable(); }

    void checkTypesOfKeys(const Block & left_block) const override
    {
        if (table_join->getClauses().size() != 1)
            throw Exception("FullSortingMergeJoin supports only one join key", ErrorCodes::NOT_IMPLEMENTED);

        const auto & onexpr = table_join->getOnlyClause();

        for (size_t i = 0; i < onexpr.key_names_left.size(); ++i)
        {
            DataTypePtr left_type = left_block.getByName(onexpr.key_names_left[i]).type;
            DataTypePtr right_type = right_sample_block.getByName(onexpr.key_names_right[i]).type;

            if (!removeNullable(left_type)->equals(*removeNullable(right_type)))
            {
                DataTypePtr left_type_no_lc = removeNullable(recursiveRemoveLowCardinality(left_type));
                DataTypePtr right_type_no_lc = removeNullable(recursiveRemoveLowCardinality(right_type));
                /// if types equal after removing low cardinality, then it is ok and can be supported
                bool equals_up_to_lc = left_type_no_lc->equals(*right_type_no_lc);
                throw DB::Exception(
                    equals_up_to_lc ? ErrorCodes::NOT_IMPLEMENTED : ErrorCodes::TYPE_MISMATCH,
                    "Type mismatch of columns to JOIN by: {} :: {} at left, {} :: {} at right",
                    onexpr.key_names_left[i], left_type->getName(),
                    onexpr.key_names_right[i], right_type->getName());
            }
        }
    }

    /// Used just to get result header
    void joinBlock(Block & block, std::shared_ptr<ExtraBlock> & /* not_processed */) override
    {
        for (const auto & col : right_sample_block)
            block.insert(col);
        block = materializeBlock(block).cloneEmpty();
    }

    void setTotals(const Block & block) override { totals = block; }
    const Block & getTotals() const override { return totals; }

    size_t getTotalRowCount() const override { __builtin_unreachable(); }
    size_t getTotalByteCount() const override { __builtin_unreachable(); }
    bool alwaysReturnsEmptySet() const override { __builtin_unreachable(); }

    std::shared_ptr<NotJoinedBlocks>
    getNonJoinedBlocks(const Block & /* left_sample_block */, const Block & /* result_sample_block */, UInt64 /* max_block_size */) const override
    {
        __builtin_unreachable();
    }

    virtual JoinPipelineType pipelineType() const override { return JoinPipelineType::YShaped; }

private:
    std::shared_ptr<TableJoin> table_join;
    Block right_sample_block;
    Block totals;
};

}
