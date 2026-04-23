#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

using namespace std;

const int MAX_KEY_LEN = 64;
const int ORDER = 50;
const int NODE_SIZE = 4096;

const int INTERNAL = 0;
const int LEAF = 1;

struct Node {
    int type;
    int count;
    int parent;
    int next;
    char keys[ORDER][MAX_KEY_LEN + 1];
    int children[ORDER + 1];
    int values[ORDER];

    Node() {
        type = LEAF;
        count = 0;
        parent = -1;
        next = -1;
        memset(keys, 0, sizeof(keys));
        memset(children, -1, sizeof(children));
        memset(values, 0, sizeof(values));
    }
};

struct Header {
    int rootPos;
    int nodeCount;
    int freeList;
    int padding;
};

class BPTree {
private:
    string filename;
    fstream file;
    Header header;

    void writeHeader() {
        file.seekp(0, ios::beg);
        file.write((char*)&header, sizeof(Header));
    }

    void readHeader() {
        file.seekg(0, ios::beg);
        file.read((char*)&header, sizeof(Header));
    }

    void writeNode(int pos, const Node& node) {
        file.seekp(sizeof(Header) + pos * NODE_SIZE, ios::beg);
        char buffer[NODE_SIZE];
        memset(buffer, 0, NODE_SIZE);
        memcpy(buffer, &node, sizeof(Node));
        file.write(buffer, NODE_SIZE);
        file.flush();
    }

    void readNode(int pos, Node& node) {
        file.seekg(sizeof(Header) + pos * NODE_SIZE, ios::beg);
        char buffer[NODE_SIZE];
        memset(buffer, 0, NODE_SIZE);
        file.read(buffer, NODE_SIZE);
        memcpy(&node, buffer, sizeof(Node));
    }

    int allocateNode() {
        if (header.freeList != -1) {
            int pos = header.freeList;
            Node temp;
            readNode(pos, temp);
            header.freeList = temp.next;
            writeHeader();
            return pos;
        }
        int pos = header.nodeCount++;
        writeHeader();
        return pos;
    }

    void freeNode(int pos) {
        Node temp;
        readNode(pos, temp);
        temp.next = header.freeList;
        header.freeList = pos;
        writeHeader();
        writeNode(pos, temp);
    }

    int findLeaf(const string& key) {
        int pos = header.rootPos;
        Node node;
        while (true) {
            readNode(pos, node);
            if (node.type == LEAF) break;
            int i = 0;
            while (i < node.count && key > node.keys[i]) i++;
            if (i > node.count) i = node.count;
            pos = node.children[i];
        }
        return pos;
    }

    void insertToLeaf(Node& leaf, const string& key, int value) {
        int i = leaf.count - 1;
        while (i >= 0 && key < leaf.keys[i]) {
            strncpy(leaf.keys[i + 1], leaf.keys[i], MAX_KEY_LEN);
            leaf.keys[i + 1][MAX_KEY_LEN] = '\0';
            leaf.values[i + 1] = leaf.values[i];
            i--;
        }
        strncpy(leaf.keys[i + 1], key.c_str(), MAX_KEY_LEN);
        leaf.keys[i + 1][MAX_KEY_LEN] = '\0';
        leaf.values[i + 1] = value;
        leaf.count++;
    }

    void insertToInternal(Node& node, const string& key, int childPos) {
        int i = node.count - 1;
        while (i >= 0 && key < node.keys[i]) {
            strncpy(node.keys[i + 1], node.keys[i], MAX_KEY_LEN);
            node.keys[i + 1][MAX_KEY_LEN] = '\0';
            node.children[i + 2] = node.children[i + 1];
            i--;
        }
        strncpy(node.keys[i + 1], key.c_str(), MAX_KEY_LEN);
        node.keys[i + 1][MAX_KEY_LEN] = '\0';
        node.children[i + 2] = childPos;
        node.count++;
    }

    void splitLeaf(int leafPos, string& upKey, int& newLeafPos) {
        Node leaf, newLeaf;
        readNode(leafPos, leaf);

        newLeafPos = allocateNode();
        newLeaf.type = LEAF;
        newLeaf.parent = leaf.parent;
        newLeaf.next = leaf.next;
        leaf.next = newLeafPos;

        int mid = leaf.count / 2;
        for (int i = mid; i < leaf.count; i++) {
            strncpy(newLeaf.keys[newLeaf.count], leaf.keys[i], MAX_KEY_LEN);
            newLeaf.keys[newLeaf.count][MAX_KEY_LEN] = '\0';
            newLeaf.values[newLeaf.count] = leaf.values[i];
            newLeaf.count++;
        }
        leaf.count = mid;

        upKey = newLeaf.keys[0];

        writeNode(leafPos, leaf);
        writeNode(newLeafPos, newLeaf);
    }

    void splitInternal(int nodePos, string& upKey, int& newChildPos) {
        Node node, newNode;
        readNode(nodePos, node);

        newChildPos = allocateNode();
        newNode.type = INTERNAL;
        newNode.parent = node.parent;

        int mid = node.count / 2;
        upKey = node.keys[mid];

        newNode.count = 0;
        for (int i = mid + 1; i <= node.count; i++) {
            newNode.children[newNode.count] = node.children[i];
            Node child;
            readNode(node.children[i], child);
            child.parent = newChildPos;
            writeNode(node.children[i], child);
            newNode.count++;
        }
        newNode.count--;
        for (int i = mid + 1; i < node.count; i++) {
            strncpy(newNode.keys[i - mid - 1], node.keys[i], MAX_KEY_LEN);
            newNode.keys[i - mid - 1][MAX_KEY_LEN] = '\0';
        }

        node.count = mid;

        writeNode(nodePos, node);
        writeNode(newChildPos, newNode);
    }

    void insertToParent(int nodePos, const string& key, int childPos) {
        Node node;
        readNode(nodePos, node);

        if (node.parent == -1) {
            int newRootPos = allocateNode();
            Node newRoot;
            newRoot.type = INTERNAL;
            newRoot.count = 1;
            strncpy(newRoot.keys[0], key.c_str(), MAX_KEY_LEN);
            newRoot.keys[0][MAX_KEY_LEN] = '\0';
            newRoot.children[0] = nodePos;
            newRoot.children[1] = childPos;
            newRoot.parent = -1;

            node.parent = newRootPos;
            Node child;
            readNode(childPos, child);
            child.parent = newRootPos;

            header.rootPos = newRootPos;
            writeHeader();
            writeNode(nodePos, node);
            writeNode(childPos, child);
            writeNode(newRootPos, newRoot);
            return;
        }

        int parentPos = node.parent;
        Node parent;
        readNode(parentPos, parent);

        if (parent.count < ORDER) {
            insertToInternal(parent, key, childPos);
            writeNode(parentPos, parent);
        } else {
            string upKey;
            int newParentPos;
            splitInternal(parentPos, upKey, newParentPos);

            Node newParent;
            readNode(newParentPos, newParent);

            if (key < upKey) {
                insertToInternal(parent, key, childPos);
                writeNode(parentPos, parent);
            } else {
                insertToInternal(newParent, key, childPos);
                writeNode(newParentPos, newParent);
            }

            insertToParent(parentPos, upKey, newParentPos);
        }
    }

    void removeFromLeaf(Node& leaf, int idx) {
        for (int i = idx; i < leaf.count - 1; i++) {
            strncpy(leaf.keys[i], leaf.keys[i + 1], MAX_KEY_LEN);
            leaf.keys[i][MAX_KEY_LEN] = '\0';
            leaf.values[i] = leaf.values[i + 1];
        }
        leaf.count--;
    }

    void removeFromInternal(Node& node, int idx) {
        for (int i = idx; i < node.count - 1; i++) {
            strncpy(node.keys[i], node.keys[i + 1], MAX_KEY_LEN);
            node.keys[i][MAX_KEY_LEN] = '\0';
        }
        for (int i = idx + 1; i <= node.count; i++) {
            node.children[i] = node.children[i + 1];
        }
        node.count--;
    }

    void borrowFromLeft(Node& node, Node& leftSibling, int parentPos, int idx) {
        if (node.type == LEAF) {
            for (int i = node.count; i > 0; i--) {
                strncpy(node.keys[i], node.keys[i - 1], MAX_KEY_LEN);
                node.keys[i][MAX_KEY_LEN] = '\0';
                node.values[i] = node.values[i - 1];
            }
            strncpy(node.keys[0], leftSibling.keys[leftSibling.count - 1], MAX_KEY_LEN);
            node.keys[0][MAX_KEY_LEN] = '\0';
            node.values[0] = leftSibling.values[leftSibling.count - 1];
            node.count++;
            leftSibling.count--;

            Node parent;
            readNode(parentPos, parent);
            strncpy(parent.keys[idx], node.keys[0], MAX_KEY_LEN);
            parent.keys[idx][MAX_KEY_LEN] = '\0';
            writeNode(parentPos, parent);
        } else {
            for (int i = node.count; i > 0; i--) {
                strncpy(node.keys[i], node.keys[i - 1], MAX_KEY_LEN);
                node.keys[i][MAX_KEY_LEN] = '\0';
            }
            for (int i = node.count + 1; i > 0; i--) {
                node.children[i] = node.children[i - 1];
            }
            strncpy(node.keys[0], leftSibling.keys[leftSibling.count - 1], MAX_KEY_LEN);
            node.keys[0][MAX_KEY_LEN] = '\0';
            node.children[0] = leftSibling.children[leftSibling.count];
            node.count++;

            Node parent;
            readNode(parentPos, parent);
            strncpy(parent.keys[idx], leftSibling.keys[leftSibling.count - 1], MAX_KEY_LEN);
            parent.keys[idx][MAX_KEY_LEN] = '\0';
            writeNode(parentPos, parent);

            leftSibling.count--;
        }
    }

    void borrowFromRight(Node& node, Node& rightSibling, int parentPos, int idx) {
        if (node.type == LEAF) {
            strncpy(node.keys[node.count], rightSibling.keys[0], MAX_KEY_LEN);
            node.keys[node.count][MAX_KEY_LEN] = '\0';
            node.values[node.count] = rightSibling.values[0];
            node.count++;

            for (int i = 0; i < rightSibling.count - 1; i++) {
                strncpy(rightSibling.keys[i], rightSibling.keys[i + 1], MAX_KEY_LEN);
                rightSibling.keys[i][MAX_KEY_LEN] = '\0';
                rightSibling.values[i] = rightSibling.values[i + 1];
            }
            rightSibling.count--;

            Node parent;
            readNode(parentPos, parent);
            strncpy(parent.keys[idx], rightSibling.keys[0], MAX_KEY_LEN);
            parent.keys[idx][MAX_KEY_LEN] = '\0';
            writeNode(parentPos, parent);
        } else {
            strncpy(node.keys[node.count], rightSibling.keys[0], MAX_KEY_LEN);
            node.keys[node.count][MAX_KEY_LEN] = '\0';
            node.children[node.count + 1] = rightSibling.children[0];
            node.count++;

            for (int i = 0; i < rightSibling.count - 1; i++) {
                strncpy(rightSibling.keys[i], rightSibling.keys[i + 1], MAX_KEY_LEN);
                rightSibling.keys[i][MAX_KEY_LEN] = '\0';
            }
            for (int i = 0; i <= rightSibling.count; i++) {
                rightSibling.children[i] = rightSibling.children[i + 1];
            }

            Node parent;
            readNode(parentPos, parent);
            strncpy(parent.keys[idx], rightSibling.keys[0], MAX_KEY_LEN);
            parent.keys[idx][MAX_KEY_LEN] = '\0';
            writeNode(parentPos, parent);

            rightSibling.count--;
        }
    }

    void mergeNodes(Node& left, Node& right, int parentPos, int idx) {
        if (left.type == LEAF) {
            for (int i = 0; i < right.count; i++) {
                strncpy(left.keys[left.count], right.keys[i], MAX_KEY_LEN);
                left.keys[left.count][MAX_KEY_LEN] = '\0';
                left.values[left.count] = right.values[i];
                left.count++;
            }
            left.next = right.next;
        } else {
            strncpy(left.keys[left.count], right.keys[0], MAX_KEY_LEN);
            left.keys[left.count][MAX_KEY_LEN] = '\0';
            left.count++;
            for (int i = 0; i < right.count; i++) {
                strncpy(left.keys[left.count], right.keys[i], MAX_KEY_LEN);
                left.keys[left.count][MAX_KEY_LEN] = '\0';
                left.children[left.count] = right.children[i];
                Node child;
                readNode(right.children[i], child);
                child.parent = -1;
                writeNode(right.children[i], child);
                left.count++;
            }
            left.children[left.count] = right.children[right.count];
            Node child;
            readNode(right.children[right.count], child);
            child.parent = -1;
            writeNode(right.children[right.count], child);
        }
    }

    void rebalance(int nodePos) {
        if (nodePos == header.rootPos) {
            Node root;
            readNode(nodePos, root);
            if (root.count == 0 && root.type == INTERNAL) {
                header.rootPos = root.children[0];
                writeHeader();
            }
            return;
        }

        Node node;
        readNode(nodePos, node);

        int minKeys = ORDER / 2 - 1;
        if (node.count >= minKeys) return;

        int parentPos = node.parent;
        if (parentPos == -1) return;

        Node parent;
        readNode(parentPos, parent);

        int idx = 0;
        while (idx <= parent.count && parent.children[idx] != nodePos) idx++;

        Node leftSibling, rightSibling;
        int leftPos = -1, rightPos = -1;
        if (idx > 0) {
            leftPos = parent.children[idx - 1];
            readNode(leftPos, leftSibling);
        }
        if (idx < parent.count) {
            rightPos = parent.children[idx + 1];
            readNode(rightPos, rightSibling);
        }

        if (leftPos != -1 && leftSibling.count > minKeys) {
            borrowFromLeft(node, leftSibling, parentPos, idx - 1);
            writeNode(nodePos, node);
            writeNode(leftPos, leftSibling);
            return;
        }

        if (rightPos != -1 && rightSibling.count > minKeys) {
            borrowFromRight(node, rightSibling, parentPos, idx);
            writeNode(nodePos, node);
            writeNode(rightPos, rightSibling);
            return;
        }

        if (leftPos != -1) {
            mergeNodes(leftSibling, node, parentPos, idx - 1);
            writeNode(leftPos, leftSibling);
            freeNode(nodePos);
            removeFromInternal(parent, idx - 1);
            writeNode(parentPos, parent);
            rebalance(parentPos);
        } else if (rightPos != -1) {
            mergeNodes(node, rightSibling, parentPos, idx);
            writeNode(nodePos, node);
            freeNode(rightPos);
            removeFromInternal(parent, idx);
            writeNode(parentPos, parent);
            rebalance(parentPos);
        }
    }

    void deleteEntry(int nodePos, const string& key) {
        Node node;
        readNode(nodePos, node);

        if (node.type == LEAF) {
            int idx = -1;
            for (int i = 0; i < node.count; i++) {
                if (strcmp(node.keys[i], key.c_str()) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx != -1) {
                removeFromLeaf(node, idx);
                writeNode(nodePos, node);
            }
        } else {
            int idx = 0;
            while (idx < node.count && key >= node.keys[idx]) idx++;
            if (idx > node.count) idx = node.count;
            deleteEntry(node.children[idx], key);
        }

        if (nodePos == header.rootPos) {
            Node root;
            readNode(nodePos, root);
            if (root.count == 0 && root.type == INTERNAL) {
                header.rootPos = root.children[0];
                writeHeader();
            }
            return;
        }

        int minKeys = ORDER / 2 - 1;
        if (node.count >= minKeys) return;

        int parentPos = node.parent;
        if (parentPos == -1) return;

        Node parent;
        readNode(parentPos, parent);

        int idx = 0;
        while (idx <= parent.count && parent.children[idx] != nodePos) idx++;

        Node leftSibling, rightSibling;
        int leftPos = -1, rightPos = -1;
        if (idx > 0) {
            leftPos = parent.children[idx - 1];
            readNode(leftPos, leftSibling);
        }
        if (idx < parent.count) {
            rightPos = parent.children[idx + 1];
            readNode(rightPos, rightSibling);
        }

        if (leftPos != -1 && leftSibling.count > minKeys) {
            borrowFromLeft(node, leftSibling, parentPos, idx - 1);
            writeNode(nodePos, node);
            writeNode(leftPos, leftSibling);
            return;
        }

        if (rightPos != -1 && rightSibling.count > minKeys) {
            borrowFromRight(node, rightSibling, parentPos, idx);
            writeNode(nodePos, node);
            writeNode(rightPos, rightSibling);
            return;
        }

        if (leftPos != -1) {
            mergeNodes(leftSibling, node, parentPos, idx - 1);
            writeNode(leftPos, leftSibling);
            freeNode(nodePos);
            removeFromInternal(parent, idx - 1);
            writeNode(parentPos, parent);
            rebalance(parentPos);
        } else if (rightPos != -1) {
            mergeNodes(node, rightSibling, parentPos, idx);
            writeNode(nodePos, node);
            freeNode(rightPos);
            removeFromInternal(parent, idx);
            writeNode(parentPos, parent);
            rebalance(parentPos);
        }
    }

public:
    BPTree(const string& fname) : filename(fname) {
        file.open(filename, ios::in | ios::out | ios::binary);
        if (!file.is_open()) {
            file.open(filename, ios::out | ios::binary);
            file.close();
            file.open(filename, ios::in | ios::out | ios::binary);
            header.rootPos = 1;
            header.nodeCount = 1;
            header.freeList = -1;
            writeHeader();
            Node root;
            root.type = LEAF;
            writeNode(1, root);
        } else {
            file.seekg(0, ios::end);
            if (file.tellg() == 0) {
                header.rootPos = 1;
                header.nodeCount = 1;
                header.freeList = -1;
                writeHeader();
                Node root;
                root.type = LEAF;
                writeNode(1, root);
            } else {
                readHeader();
            }
        }
    }

    ~BPTree() {
        if (file.is_open()) file.close();
    }

    void insert(const string& key, int value) {
        int leafPos = findLeaf(key);
        Node leaf;
        readNode(leafPos, leaf);

        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.keys[i], key.c_str()) == 0 && leaf.values[i] == value) {
                return;
            }
        }

        if (leaf.count < ORDER) {
            insertToLeaf(leaf, key, value);
            writeNode(leafPos, leaf);
        } else {
            insertToLeaf(leaf, key, value);
            writeNode(leafPos, leaf);

            string upKey;
            int newLeafPos;
            splitLeaf(leafPos, upKey, newLeafPos);
            insertToParent(leafPos, upKey, newLeafPos);
        }
    }

    void remove(const string& key, int value) {
        int leafPos = findLeaf(key);
        Node leaf;
        readNode(leafPos, leaf);

        int idx = -1;
        for (int i = 0; i < leaf.count; i++) {
            if (strcmp(leaf.keys[i], key.c_str()) == 0 && leaf.values[i] == value) {
                idx = i;
                break;
            }
        }

        if (idx != -1) {
            removeFromLeaf(leaf, idx);
            writeNode(leafPos, leaf);
            deleteEntry(leafPos, key);
        }
    }

    vector<int> find(const string& key) {
        vector<int> result;
        int leafPos = findLeaf(key);
        Node leaf;
        readNode(leafPos, leaf);

        while (leafPos != -1) {
            readNode(leafPos, leaf);
            for (int i = 0; i < leaf.count; i++) {
                if (strcmp(leaf.keys[i], key.c_str()) == 0) {
                    result.push_back(leaf.values[i]);
                }
            }
            if (leaf.count > 0 && strcmp(leaf.keys[leaf.count - 1], key.c_str()) == 0) {
                leafPos = leaf.next;
            } else {
                break;
            }
        }

        sort(result.begin(), result.end());
        return result;
    }
};

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    BPTree tree("bptree.db");

    int n;
    cin >> n;

    for (int i = 0; i < n; i++) {
        string cmd;
        cin >> cmd;

        if (cmd == "insert") {
            string key;
            int value;
            cin >> key >> value;
            tree.insert(key, value);
        } else if (cmd == "delete") {
            string key;
            int value;
            cin >> key >> value;
            tree.remove(key, value);
        } else if (cmd == "find") {
            string key;
            cin >> key;
            vector<int> result = tree.find(key);
            if (result.empty()) {
                cout << "null" << endl;
            } else {
                for (size_t j = 0; j < result.size(); j++) {
                    if (j > 0) cout << " ";
                    cout << result[j];
                }
                cout << endl;
            }
        }
    }

    return 0;
}
