struct INode {
};

struct ID {
};

extern int foo();
extern INode *load_inode (const ID *id, int version);
extern ID*    id_root();
