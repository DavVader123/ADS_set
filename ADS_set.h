#ifndef ADS_SET_H
#define ADS_SET_H

#include <algorithm>
#include <functional>
#include <iostream>

#if defined(DEBUG)
#define TRACE(msg) std::cerr << msg << std::endl;
#define TRACE_IF(condition, msg) \
    if (condition) { \
        TRACE(msg) \
    }
#else
#define TRACE(msg)
#define TRACE_IF(condition, msg)
#endif

template<typename Key, size_t N = 3>
class ADS_set {
    public:
        class Iterator;
        using value_type = Key;
        using key_type = Key;
        using reference = value_type &;
        using const_reference = const value_type &;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using const_iterator = Iterator;
        using iterator = Iterator;
        using key_compare = std::less<key_type>;
        using key_equal = std::equal_to<key_type>;

    private:
        enum class InsertMsg {
            SUCCESS,
            EXISTS,
            SPLIT
        };

        enum class EraseMsg {
            SUCCESS,
            NOT_EXISTENT,
            MERGE
        };

        enum class MergeDirection {
            LEFT,
            RIGHT
        };

        enum class NodeType {
            INTERNAL,
            EXTERNAL
        };

        struct Node {
                static constexpr size_type max_size {N * 2};
                static constexpr size_type min_size {N};
                key_type *values;
                size_type node_size;

                // size of the value array so that an additional key has place right before split
                Node(): values {new key_type[max_size + 1]}, node_size {0} {};

                virtual ~Node() {
                    delete[] values;
                };

                virtual void split(size_type) = 0;

                virtual InsertMsg add_elem(const key_type &) = 0;

                virtual NodeType type() const = 0;

                virtual bool count(const key_type &) const = 0;

                virtual iterator find(const key_type &key) const = 0;

                virtual EraseMsg remove_elem(const key_type &) = 0;

                virtual void dump(std::ostream &, size_t) const = 0;
        };

        struct ExternalNode : public Node {
                ExternalNode *left_neighbour;
                ExternalNode *right_neighbour;

                ExternalNode(): Node(), left_neighbour {nullptr}, right_neighbour {nullptr} {}

                ExternalNode(ExternalNode *left_neighbour, ExternalNode *right_neighbour)
                    : Node(), left_neighbour {left_neighbour}, right_neighbour {right_neighbour} {}

                void split(size_type) override {
                    TRACE("Something went wrong! (Split was called in External Node!)");
                }

                NodeType type() const override {
                    return NodeType::EXTERNAL;
                }

                bool count(const key_type &elem) const override {
                    if (find_pos(elem) == -1) {
                        return false;
                    }
                    return true;
                }

                int find_pos(const key_type &elem) const {
                    if (this->node_size == 0) {
                        return -1;
                    }
                    int low {0};
                    int high {static_cast<int>(this->node_size) - 1};
                    while (low <= high) {
                        int mid = low + (high - low) / 2;
                        if (key_equal {}(this->values[mid], elem)) {
                            return mid;
                        }
                        if (key_compare {}(this->values[mid], elem)) {
                            low = mid + 1;
                        } else {
                            high = mid - 1;
                        }
                    }

                    return -1;
                }

                iterator find(const key_type &elem) const override {
                    if (this->node_size == 0) {
                        return Iterator();
                    }
                    int i {find_pos(elem)};
                    if (i != -1) {
                        return Iterator(this, i);
                    }
                    return Iterator();
                }

                InsertMsg add_elem(const key_type &elem) override {
                    if (find_pos(elem) != -1) {
                        return InsertMsg::EXISTS;
                    }
                    size_type i {0};
                    for (; i < this->node_size; ++i) {
                        if (key_compare {}(elem, this->values[i])) {
                            break;
                        }
                    }
                    for (size_type j {this->node_size}; j > i; --j) {
                        this->values[j] = this->values[j - 1];
                    }
                    this->values[i] = elem;
                    if (++this->node_size > this->max_size) {
                        return InsertMsg::SPLIT;
                    }
                    return InsertMsg::SUCCESS;
                }

                EraseMsg remove_elem(const key_type &elem) override {
                    const int i {find_pos(elem)};
                    if (i == -1) {
                        return EraseMsg::NOT_EXISTENT;
                    }
                    size_type pos {static_cast<size_type>(i)};
                    for (; pos < this->node_size - 1; ++pos) {
                        this->values[pos] = this->values[pos + 1];
                    }
                    if (--this->node_size < this->min_size) {
                        return EraseMsg::MERGE;
                    }
                    return EraseMsg::SUCCESS;
                }

                void dump(std::ostream &o, const size_t n) const override {
                    for (size_t s {0}; s < n; ++s) {
                        o << "    ";
                    }
                    o << "Leaf: [";
                    if (this->node_size > 0) {
                        o << this->values[0];
                        for (size_t i {1}; i < this->node_size; ++i) {
                            o << ", " << this->values[i];
                        }
                    }

                    o << ']' << std::endl;
                }
        };

        struct InternalNode : public Node {
                Node **children;

                // size of children array +2 so that node can have 1 key too much right before split
                InternalNode(): Node(), children {new Node *[Node::max_size + 2]} {}

                ~InternalNode() override {
                    for (size_t i {0}; i <= this->node_size; ++i) {
                        delete children[i];
                    }
                    delete[] children;
                }

                NodeType type() const override {
                    return NodeType::INTERNAL;
                }

                size_type find_pos(key_type elem) const {
                    size_type i {0};
                    while (i < this->node_size) {
                        if (key_compare {}(elem, this->values[i])) {
                            return i;
                        }
                        ++i;
                    }
                    return i;
                }

                bool count(const key_type &key) const override {
                    return this->children[find_pos(key)]->count(key);
                }

                iterator find(const key_type &key) const override {
                    return this->children[find_pos(key)]->find(key);
                }

                InsertMsg add_elem(const key_type &elem) override {
                    size_type pos {find_pos(elem)};

                    TRACE_IF(pos > this->node_size, "InternalNode::find_pos malfunctioned!");

                    InsertMsg result {children[pos]->add_elem(elem)};
                    if (result == InsertMsg::SPLIT) {
                        split(pos);
                        if (this->node_size <= this->max_size) {
                            result = InsertMsg::SUCCESS;
                        }
                    }
                    return result;
                }

                void split(size_type pos) override {
                    size_type e {0};
                    switch (this->children[pos]->type()) {
                        case NodeType::EXTERNAL:

                            // construct right part
                            ExternalNode *right_split_e;
                            right_split_e = new ExternalNode {dynamic_cast<ExternalNode *>(this->children[pos]),
                                                              dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour};
                            for (; e <= this->children[pos]->node_size / 2; ++e) {
                                right_split_e->values[e] = this->children[pos]->values[e + this->children[pos]->node_size / 2];
                            }
                            right_split_e->node_size = e;

                            // cut left part
                            this->children[pos]->node_size /= 2;

                            // manage leaf chaining
                            if (dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour != nullptr) {
                                dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour->left_neighbour = right_split_e;
                            }
                            dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour = right_split_e;

                            // insert new node
                            for (size_type i {this->node_size}; i > pos; --i) {
                                this->values[i] = this->values[i - 1];
                                this->children[i + 1] = this->children[i];
                            }
                            this->values[pos] = right_split_e->values[0];
                            this->children[pos + 1] = std::move(right_split_e);
                            ++this->node_size;
                            break;

                        case NodeType::INTERNAL:

                            // new key
                            key_type new_key {this->children[pos]->values[this->children[pos]->node_size / 2]};

                            // construct right part
                            InternalNode *right_split_i {new InternalNode()};
                            size_type i {0};
                            for (; i < this->children[pos]->node_size / 2; ++i) {
                                right_split_i->children[i]
                                    = dynamic_cast<InternalNode *>(this->children[pos])->children[i + this->children[pos]->node_size / 2 + 1];
                                right_split_i->values[i] = this->children[pos]->values[i + this->children[pos]->node_size / 2 + 1];
                            }
                            right_split_i->children[i]
                                = dynamic_cast<InternalNode *>(this->children[pos])->children[i + this->children[pos]->node_size / 2 + 1];
                            right_split_i->node_size = i;

                            // cut left part
                            this->children[pos]->node_size /= 2;

                            // insert new node
                            for (size_type j {this->node_size}; j > pos; --j) {
                                this->values[j] = this->values[j - 1];
                                this->children[j + 1] = this->children[j];
                            }
                            this->values[pos] = std::move(new_key);
                            this->children[pos + 1] = std::move(right_split_i);
                            ++this->node_size;
                            break;
                    }
                }

                EraseMsg remove_elem(const key_type &elem) override {
                    size_type pos {find_pos(elem)};

                    TRACE_IF(pos > this->node_size, "InternalNode::find_pos malfunctioned!");

                    EraseMsg result {this->children[pos]->remove_elem(elem)};
                    if (result == EraseMsg::MERGE) {
                        merge(pos);
                        if (this->node_size >= this->min_size) {
                            result = EraseMsg::SUCCESS;
                        }
                    }
                    return result;
                }

                void merge(size_type pos) {
                    MergeDirection direction; // -1 für links, +1 für rechts
                    if (pos == this->node_size || (pos != 0 && this->children[pos - 1]->node_size < this->children[pos + 1]->node_size)) {
                        direction = MergeDirection::LEFT;
                    } else {
                        direction = MergeDirection::RIGHT;
                    }
                    // try and move one key
                    if (this->children[pos]->type() == NodeType::EXTERNAL) {
                        if ((pos == 0 || (direction == MergeDirection::LEFT && pos < this->node_size))
                            && this->children[pos + 1]->node_size > this->min_size) {
                            this->children[pos]->values[this->children[pos]->node_size] = this->children[pos + 1]->values[0];
                            for (size_type i {0}; i < this->children[pos + 1]->node_size; ++i) {
                                this->children[pos + 1]->values[i] = this->children[pos + 1]->values[i + 1];
                            }
                            ++this->children[pos]->node_size;
                            --this->children[pos + 1]->node_size;
                            this->values[pos] = this->children[pos + 1]->values[0];
                            TRACE_IF(this->children[pos]->node_size < this->min_size,
                                     "SOMETHING WENT WRONG. Key was moved but size is still too low!");
                            return;
                        }
                        if ((pos == this->node_size || (direction == MergeDirection::RIGHT && pos > 0))
                            && this->children[pos - 1]->node_size > this->min_size) {
                            for (size_type i {this->children[pos]->node_size}; i-- > 0;) {
                                this->children[pos]->values[i + 1] = this->children[pos]->values[i];
                            }
                            this->children[pos]->values[0] = this->children[pos - 1]->values[this->children[pos - 1]->node_size - 1];
                            ++this->children[pos]->node_size;
                            --this->children[pos - 1]->node_size;
                            this->values[pos - 1] = this->children[pos]->values[0];
                            TRACE_IF(this->children[pos]->node_size < this->min_size,
                                     "SOMETHING WENT WRONG. Key was moved but size is still too low!");
                            return;
                        }
                    } else {
                        if ((pos == 0 || (direction == MergeDirection::LEFT && pos < this->node_size))
                            && this->children[pos + 1]->node_size > this->min_size) {
                            // move key and child into node at pos (key in parent goes to pos and first key in pos+1 goes in parent
                            this->children[pos]->values[this->children[pos]->node_size] = this->values[pos];
                            this->values[pos] = this->children[pos + 1]->values[0];
                            dynamic_cast<InternalNode *>(this->children[pos])->children[this->children[pos]->node_size + 1]
                                = dynamic_cast<InternalNode *>(this->children[pos + 1])->children[0];
                            // restructure node at pos+1; one child ahead because there is 1 more child than keys
                            dynamic_cast<InternalNode *>(this->children[pos + 1])->children[0]
                                = dynamic_cast<InternalNode *>(this->children[pos + 1])->children[1];
                            for (size_type i {0}; i < this->children[pos + 1]->node_size; ++i) {
                                this->children[pos + 1]->values[i] = this->children[pos + 1]->values[i + 1];
                                dynamic_cast<InternalNode *>(this->children[pos + 1])->children[i + 1]
                                    = dynamic_cast<InternalNode *>(this->children[pos + 1])->children[i + 2];
                            }
                            // manage node sizes
                            ++this->children[pos]->node_size;
                            --this->children[pos + 1]->node_size;
                            TRACE_IF(this->children[pos]->node_size < this->min_size,
                                     "SOMETHING WENT WRONG. Key was moved but size is still too low!");
                            return;
                        }
                        if ((pos == this->node_size || (direction == MergeDirection::RIGHT && pos > 0))
                            && this->children[pos - 1]->node_size > this->min_size) {
                            // restructure node at pos; one child ahead because there is 1 more child than keys
                            dynamic_cast<InternalNode *>(this->children[pos])->children[this->children[pos]->node_size + 1]
                                = dynamic_cast<InternalNode *>(this->children[pos])->children[this->children[pos]->node_size];
                            for (size_type i {this->children[pos]->node_size}; i-- > 0;) {
                                this->children[pos]->values[i + 1] = this->children[pos]->values[i];
                                dynamic_cast<InternalNode *>(this->children[pos])->children[i + 1]
                                    = dynamic_cast<InternalNode *>(this->children[pos])->children[i];
                            }
                            // move key and child into node at pos (key in parent goes to pos and last key in pos-1 goes to parent
                            this->children[pos]->values[0] = this->values[pos - 1];
                            this->values[pos - 1] = this->children[pos - 1]->values[this->children[pos - 1]->node_size - 1];
                            dynamic_cast<InternalNode *>(this->children[pos])->children[0]
                                = dynamic_cast<InternalNode *>(this->children[pos - 1])->children[this->children[pos - 1]->node_size];
                            // manage node sizes
                            ++this->children[pos]->node_size;
                            --this->children[pos - 1]->node_size;
                            TRACE_IF(this->children[pos]->node_size < this->min_size,
                                     "SOMETHING WENT WRONG. Key was moved but size is still too low!");
                            return;
                        }
                    }
                    size_t i_left {0};
                    size_t j_left {pos};
                    size_t i_right {0};
                    size_t j_right {pos + 1};
                    switch (direction) {
                        case MergeDirection::LEFT:
                            TRACE_IF(this->children[pos]->node_size + this->children[pos + 1]->node_size > this->max_size + this->min_size,
                                     "You messed up merged node is too big!");
                            // move keys and children
                            switch (this->children[pos]->type()) {
                                case NodeType::EXTERNAL:
                                    for (; i_left < this->children[pos]->node_size; ++i_left) {
                                        this->children[pos - 1]->values[i_left + this->children[pos - 1]->node_size]
                                            = this->children[pos]->values[i_left];
                                    }
                                    // update leaf chaining
                                    dynamic_cast<ExternalNode *>(this->children[pos - 1])->right_neighbour
                                        = dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour;
                                    if (dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour) {
                                        dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour->left_neighbour
                                            = dynamic_cast<ExternalNode *>(this->children[pos - 1]);
                                    }
                                    // update size
                                    this->children[pos - 1]->node_size += this->children[pos]->node_size;
                                    break;

                                case NodeType::INTERNAL:
                                    this->children[pos - 1]->values[i_right + this->children[pos - 1]->node_size] = this->values[pos - 1];
                                    for (; i_left < this->children[pos]->node_size; ++i_left) {
                                        this->children[pos - 1]->values[i_left + this->children[pos - 1]->node_size + 1]
                                            = this->children[pos]->values[i_left];
                                        dynamic_cast<InternalNode *>(this->children[pos - 1])
                                            ->children[i_left + this->children[pos - 1]->node_size + 1]
                                            = dynamic_cast<InternalNode *>(this->children[pos])->children[i_left];
                                    }
                                    dynamic_cast<InternalNode *>(this->children[pos - 1])->children[i_left + this->children[pos - 1]->node_size + 1]
                                        = dynamic_cast<InternalNode *>(this->children[pos])->children[i_left];
                                    // update size
                                    this->children[pos - 1]->node_size += this->children[pos]->node_size + 1; // +1 because of pulled down value
                            }

                            // delete old stuff
                            if (this->children[pos]->type() == NodeType::EXTERNAL) {
                                delete children[pos];
                            } else {
                                children[pos]->node_size = 0;
                                dynamic_cast<InternalNode *>(children[pos])->children[0] = nullptr;
                                delete children[pos];
                            }

                            // reorganize parent node
                            for (; j_left < this->node_size - 1; ++j_left) {
                                this->values[j_left - 1] = this->values[j_left];
                                children[j_left] = this->children[j_left + 1];
                            }
                            this->children[j_left] = this->children[j_left + 1];
                            --this->node_size;
                            if (this->children[pos - 1]->node_size > this->max_size) {
                                TRACE_IF(this->children[pos - 1]->node_size > this->max_size + 1,
                                         "Split was called in a merg with node_size > 2k+1!");
                                split(pos - 1);
                            }
                            break;

                        case MergeDirection::RIGHT:
                            TRACE_IF(this->children[pos]->node_size + this->children[pos + 1]->node_size > this->max_size + this->min_size,
                                     "You messed up merged node is too big!");
                            // move keys and children
                            switch (this->children[pos]->type()) {
                                case NodeType::EXTERNAL:
                                    for (; i_right < this->children[pos + 1]->node_size; ++i_right) {
                                        this->children[pos]->values[i_right + this->children[pos]->node_size]
                                            = this->children[pos + 1]->values[i_right];
                                    }
                                    // update leaf chaining
                                    dynamic_cast<ExternalNode *>(this->children[pos])->right_neighbour
                                        = dynamic_cast<ExternalNode *>(this->children[pos + 1])->right_neighbour;
                                    if (dynamic_cast<ExternalNode *>(this->children[pos + 1])->right_neighbour) {
                                        dynamic_cast<ExternalNode *>(this->children[pos + 1])->right_neighbour->left_neighbour
                                            = dynamic_cast<ExternalNode *>(this->children[pos]);
                                    }
                                    // update size
                                    this->children[pos]->node_size += this->children[pos + 1]->node_size;
                                    break;
                                case NodeType::INTERNAL:
                                    /* this statement is because when an internal node is merged the key in parent which
                                      will be discarded needs to be pulled down into the merged node */
                                    this->children[pos]->values[i_right + this->children[pos]->node_size] = this->values[pos];
                                    // move rest of the stuff
                                    for (; i_right < this->children[pos + 1]->node_size; ++i_right) {
                                        this->children[pos]->values[i_right + this->children[pos]->node_size + 1]
                                            = this->children[pos + 1]->values[i_right];
                                        dynamic_cast<InternalNode *>(this->children[pos])->children[i_right + this->children[pos]->node_size + 1]
                                            = dynamic_cast<InternalNode *>(this->children[pos + 1])->children[i_right];
                                    }
                                    // move last child as well
                                    dynamic_cast<InternalNode *>(this->children[pos])->children[i_right + this->children[pos]->node_size + 1]
                                        = dynamic_cast<InternalNode *>(this->children[pos + 1])->children[i_right];
                                    // update size
                                    this->children[pos]->node_size += this->children[pos + 1]->node_size + 1; // +1 because of pulled down value
                            }

                            // delete old stuff
                            if (this->children[pos]->type() == NodeType::EXTERNAL) {
                                delete children[pos + 1];
                            } else {
                                children[pos + 1]->node_size = 0;
                                dynamic_cast<InternalNode *>(children[pos + 1])->children[0] = nullptr;
                                delete children[pos + 1];
                            }
                            // reorganize parent node
                            for (; j_right < this->node_size; ++j_right) {
                                this->values[j_right - 1] = this->values[j_right];
                                this->children[j_right] = this->children[j_right + 1];
                            }
                            this->children[j_right] = this->children[j_right + 1];
                            --this->node_size;
                            if (this->children[pos]->node_size > this->max_size) {
                                TRACE_IF(this->children[pos]->node_size > this->max_size + 1, "Split was called in a merg with node_size > 2k+1!");
                                split(pos);
                            }
                    }
                }

                void dump(std::ostream &o, const size_t n) const override {
                    for (size_t s {0}; s < n; ++s) {
                        o << "    ";
                    }
                    o << "Internal[";
                    if (this->node_size > 0) {
                        o << this->values[0];
                        for (size_t i {1}; i < this->node_size; ++i) {
                            o << ", " << this->values[i];
                        }
                    }

                    o << "]" << std::endl;
                    for (size_t i {0}; i < this->node_size + 1; ++i) {
                        this->children[i]->dump(o, n + 1);
                    }
                }
        };

        size_type sz;
        Node *root;
        ExternalNode *left_leaf;

    public:
        ADS_set(): sz {0}, root {new ExternalNode()}, left_leaf {nullptr} {
            left_leaf = dynamic_cast<ExternalNode *>(root);
        }

        ADS_set(std::initializer_list<key_type> ilist): ADS_set() {
            for (const auto &elem: ilist) {
                insert(elem);
            }
        }

        template<typename InputIt>
        ADS_set(InputIt first, InputIt last): ADS_set() {
            for (; first != last; ++first) {
                insert(*first);
            }
        }

        ADS_set(const ADS_set &other): ADS_set() {
            insert(other.begin(), other.end());
        }

        ~ADS_set() {
            delete root;
        };

        ADS_set &operator=(const ADS_set &other) {
            clear();
            insert(other.begin(), other.end());
            return *this;
        }

        ADS_set &operator=(std::initializer_list<key_type> ilist) {
            clear();
            insert(ilist);
            return *this;
        }

        size_type size() const {
            return sz;
        }

        bool empty() const {
            return sz == 0;
        }

        void insert(std::initializer_list<key_type> ilist) {
            for (const auto &elem: ilist) {
                insert(elem);
            }
        }

        std::pair<iterator, bool> insert(const key_type &key) {
            switch (root->add_elem(key)) {
                case InsertMsg::EXISTS:
                    TRACE("Existing Element found")
                    return std::pair<iterator, bool> {find(key), false};
                case InsertMsg::SUCCESS:
                    ++sz;
                    return std::pair<iterator, bool> {find(key), true};
                case InsertMsg::SPLIT:
                    InternalNode *new_root;
                    new_root = new InternalNode();
                    new_root->children[0] = root;
                    root = new_root;
                    root->split(0);
                    ++sz;
                    return std::pair<iterator, bool> {find(key), true};
            }
            TRACE("You messed up big time! (ADS_set::insert(key) got to the end!)");
            return std::pair<iterator, bool> {Iterator(), true}; // will never be reached
        }

        template<typename InputIt>
        void insert(InputIt first, InputIt last) {
            for (; first != last; ++first) {
                insert(*first);
            }
        }

        void clear() {
            delete root;
            sz = 0;
            root = new ExternalNode();
            left_leaf = dynamic_cast<ExternalNode *>(root);
        }

        size_type erase(const key_type &key) {
            switch (root->remove_elem(key)) {
                case EraseMsg::SUCCESS:
                    --sz;
                    return 1;
                case EraseMsg::NOT_EXISTENT:
                    return 0;
                case EraseMsg::MERGE:
                    if (root->type() == NodeType::INTERNAL && root->node_size == 0) {
                        Node *new_root = dynamic_cast<InternalNode *>(root)->children[0];
                        dynamic_cast<InternalNode *>(root)->children[0] = nullptr;
                        delete root;
                        root = new_root;
                    }
                    --sz;
                    return 1;
            }
            return 2; // should never be reached
        }

        size_type count(const key_type &key) const {
            if (root->count(key)) {
                return 1;
            }
            return 0;
        }

        iterator find(const key_type &key) const {
            return root->find(key);
        }

        void swap(ADS_set &other) {
            std::swap(this->root, other.root);
            std::swap(this->sz, other.sz);
            std::swap(this->left_leaf, other.left_leaf);
        }

        const_iterator begin() const {
            if (sz == 0) {
                return Iterator();
            }
            return Iterator(left_leaf, 0);
        }

        const_iterator end() const {
            return Iterator();
        }

        void dump(std::ostream &o = std::cerr, size_t n = 0) const {
            o << "Size: " << sz << std::endl;
            o << "Root ";
            root->dump(o, n);
            o << std::endl << "Left ";
            left_leaf->dump(o, 0);
        }

        bool operator==(const ADS_set &rhs) const {
            if (this->sz != rhs.sz) {
                return false;
            }
            auto lit {this->begin()};
            auto rit {rhs.begin()};
            while (lit != this->end()) {
                if (!key_equal {}(*lit, *rit)) {
                    return false;
                }
                ++lit;
                ++rit;
            }
            return true;
        }

        bool operator!=(const ADS_set &rhs) const {
            return !(*this == rhs);
        }
};

template<typename Key, size_t N>
class ADS_set<Key, N>::Iterator {
    public:
        using value_type = Key;
        using difference_type = std::ptrdiff_t;
        using reference = const value_type &;
        using pointer = const value_type *;
        using iterator_category = std::forward_iterator_tag;

    private:
        const ExternalNode *current_node;
        size_type current_element;

    public:
        Iterator(): current_node {nullptr}, current_element {0} {}

        Iterator(const ExternalNode *current_node, const size_type current_element): current_node {current_node}, current_element {current_element} {}

        reference operator*() const {
            return current_node->values[current_element];
        }

        pointer operator->() const {
            return &current_node->values[current_element];
        }

        Iterator &operator++() {
            if (current_node == nullptr) {
                return *this;
            }
            if (++current_element >= current_node->node_size) {
                current_node = current_node->right_neighbour;
                current_element = 0;
            }
            return *this;
        }

        Iterator operator++(int) {
            if (current_node == nullptr) {
                return *this;
            }
            Iterator copy{this->current_node,this->current_element};
            if (++current_element >= current_node->node_size) {
                current_node = current_node->right_neighbour;
                current_element = 0;
            }
            return copy;
        }

        bool operator==(const Iterator &rhs) const {
            return this->current_node == rhs.current_node && this->current_element == rhs.current_element;
        }

        bool operator!=(const Iterator &rhs) const {
            return this->current_node != rhs.current_node || this->current_element != rhs.current_element;
        }
};

template<typename Key, size_t N>
void swap(ADS_set<Key, N> &lhs, ADS_set<Key, N> &rhs) {
    lhs.swap(rhs);
}

#endif // ADS_set