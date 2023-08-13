#if 0

RClass(Record, Multithreaded);
struct Movie
{
  RBody(); => 
    BaseTable* mParentTable = nullptr;
    friend struct MovieTable;
    mutable bool mDeletePending = false;
    enum class Column { ID, Name, Score, ReleaseYear, IMDBId, SomeSource };
    std::bitset<enum_count<Column>() + /* entire row needs flushing */1> mChanges;
    /// TODO: Do we require inherting from reflectable? We could use its Dirty flag

  RField(Key, Autoincrement);
  int ID = 0;

  RField(Index = { Collation = CaseInsensitive });
  string Name;

  RField(Index = { Sort = Descending });
  double Score = 5.0;

  RField(Index);
  int ReleaseYear = 2000;

  RField(Index, Unique);
  uint64_t IMDBId = 0; 
  => void SetIMBDId(uint64_t val) {
    mParentTable->InTransaction([this, &val] {
      mParentTable->ConstrainedSet(this, (int)Column::IMDBId, this->IMDBId, val); 
      mParentTable->Reindex(this, (int)Column::IMDBId); 
      mParentTable->Reindex(this, (int)Column::SomeSource);
    });
  }

  RMethod(Index);
  int SomeSource() const; /// Cannot have arguments, must be const

  /// Only private fields ? How to limit changing indexed fields?
};

=>

struct BaseTable
{
  std::mutex mWriteTransactionMutex;

  std::atomic_size_t mTransactionDepth{0};
}

template <typename ROW_TYPE, typename CRTP>
struct TTable : BaseTable
{
  using RowType = ROW_TYPE;
  using PrimaryKeyType = RowType::PrimaryKeyType;

  static constexpr inline RowCount = RowType::mRowCount;
    
  std::array<bool, RowCount> mIndicesInNeedOfRebuilding;

  void Reindex(ROW_TYPE* because_of, int column)
  {
    mIndicesInNeedOfRebuilding[column] = true;
  }

  void BeginTransaction()
  {
    if constexpr (RowType::Attribute_Multithreaded)
    {
      if (mTransactionDepth == 0)
        mWriteTransactionMutex.lock();
    }

    mTransactionDepth++;
  };

  void EndTransaction()
  {
    mTransactionDepth--;
    if (mTransactionDepth == 0)
    {
      for (int i=0; i<RowCount; ++i)
        if (mIndicesInNeedOfRebuilding) 
          static_cast<CRTP*>(this)->PerformReindex(i);

      if constexpr (RowType::Attribute_Multithreaded)
        mWriteTransactionMutex.unlock();
    }
  }

  std::map<PK, T, std::less<>> TableStorage;
};

struct OrderBy : SelectParam
{
  OrderBy(member function pointer, asc/desc = asc);
  OrderBy(member variable pointer, asc/desc = asc);
  OrderBy(column id, asc/desc = asc);
  OrderBy(column name, asc/desc = asc);
};

struct MovieTable : TTable<Movie, MovieTable>
{

  auto InTransaction(auto&& func) -> decltype(func(this))
  {
    BeginWriteTransaction();
    try
    {
      return func(this);
      EndWriteTransaction();
    }
    catch (e)
    {
      if (!RollbackTransaction()) /// mTransactionDepth > 1
        throw;
    }
    return {};
  }

  /// Not thread-safe
  RowType const* Get(PrimaryKeyType const& key)
  {
    auto row = Find(key);
    if (row && !row->mDeletePending)
      return row;
    return nullptr;
  }

  auto With(PrimaryKeyType const& key, auto&& func)
  {
    return InTransaction([&]{
      auto row = Get(key);
      return row ? func(*row) : {};
    });
  }

  std::optional<RowType> Delete(PrimaryKeyType const& key)
  {
    
  }
  
  /// Params are stuff like LIMIT, ORDER BY, etc.
  std::vector<RowType> DeleteWhere(auto&& func, auto... params)
  {
    return InTransaction([&] {
      MovieTableSelection selection = Select(func, params...);
      return RemoveRows(selection);
    });
  }

  void ForEach(auto&& func);

  MovieTableSelection Select(auto&& func, auto... params);

  /// group by, distinct, window, joins, having

  std::optional<PrimaryKeyType> Insert(RowType row);
  std::vector<PrimaryKeyType> Insert(std::span<RowType> rows);
  std::optional<PrimaryKeyType> InsertOr(ABORT/FAIL/IGNORE/REPLACE/ROLLBACK, RowType row);
  std::optional<PrimaryKeyType> InsertOrUpdate(RowType new_row, auto&& update_func)
  {
    if (conflict)
    {
      update_func(new_row, old_row);
      old_row->SetDirty();
    }
  }

  void Update(auto&& update_func, auto&& where, auto... params)
  {
    return InTransaction([&] {
      MovieTableSelection selection = Select(where, params...);
      for (auto& obj : selection)
      {
        update_func(obj);
        obj->SetDirty();
      }
    });
  }

private:

  struct TransactionElement
  {
    int ElementType = /// UPDATE_FIELD, DELETE_ROW;
    row_ptr Row = {};
    int ColumnId = {};
    std::any OldFieldValue;
  };

  enum {
    ROW_Name,
    ROW_Score,
    ROW_ReleaseYear,
    ROW_IMDBId,
    ROW_SomeSource,

    ROW_COUNT,
  };

  void PerformReindex(int row)
  {
    switch (row)
    {
      case ROW_Name: PerformReindex_Name();
      ...
    }
  }

  friend struct Movie;

  using RowPtr = either `PrimaryKeyType` or `RowType*`
  
  std::multimap<string, RowPtr, Reflector::Collator<CaseInsensitive>::Less> IndexFor_Name;
  std::multimap<double, RowPtr, std::greater<>> IndexFor_Score;
  std::multimap<int, RowPtr, std::less<>> IndexFor_ReleaseYear;
  std::map<uint64_t, RowPtr, std::less<>> IndexFor_IMDBId;
  std::multimap<int, RowPtr, std::less<>> IndexFor_SomeSource;
};

template <typename TABLE>
struct TTableSelection
{
  std::vector<TABLE::PrimaryKeyType> SelectedRows;

  void SortByColumns(auto... columns);
  void Crop(int start, int count);
};

struct MovieTableSelection : TTableSelection<MovieTable>
{
};


#endif