The structure of chainbase:
namespace chainbase:
    class database:
   	function:
            void open(const boost::filesystem::path& dir, uint32_t write = read_only, size_t shared_file_size = 0);
	    void close();
	    void flush();
	    void wipe(const boost::filesystem::path& dir);
	    void resize(size_t new_shared_file_size);
	    void set_require_locking(bool enable_require_locking);
	    void undo();
	    void squash();
	    void commit(int64_t revision);
	    void undo_all();
	        
	    struct session {
	    public:
	        void push() 
	        void squash() 
	        void undo()
	        ...
	     }
	    bool has_index()
	    void add_index()
	    auto get_index() 
	    const ObjectType* find(CompatibleKey&& key)
	    const ObjectType* find(object_id<ObjectType> key = object_id<ObjectType>())
	    const ObjectType& get(CompatibleKey&& key)
	    const ObjectType& get(const object_id<ObjectType>& key = object_id<ObjectType>()) const ObjectType& get(CompatibleKey&& key)
	    void modify(const ObjectType& obj, Modifier&& m)
	    void remove(const ObjectType& obj)
	    ...
