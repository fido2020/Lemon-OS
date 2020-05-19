#include <fs/tar.h>

#include <logging.h>

inline static long OctToDec(char* str, int size) {
    long n = 0;
    while (size-- && *str) {
        n <<= 3;
        n |= (*str - '0') & 0x7;
        str++;
    }
    return n;
}

inline static long GetSize(char* size){ // Get size of file in blocks
    long sz = OctToDec(size, 12);
    return sz;
}

inline static long GetBlockCount(char* size){ // Get size of file in blocks
    long sz = OctToDec(size, 12);
    long round = (sz % 512) ? (512 - (sz % 512)) : 0; // Round up to 512 byte multiple
    return (sz + round) / 512;
}

inline static uint32_t TarTypeToFilesystemFlags(char type){
    switch(type){
        case TAR_TYPE_DIRECTORY:
            return FS_NODE_DIRECTORY;
        default:
            return FS_NODE_FILE;
    }
}

namespace fs::tar{
    size_t TarNode::Read(size_t offset, size_t size, uint8_t *buffer){
        if(vol){
            return vol->Read(this, offset, size, buffer);
        } else return 0;
    }

    size_t TarNode::Write(size_t offset, size_t size, uint8_t *buffer){
        if(vol){
            return vol->Write(this, offset, size, buffer);
        } else return 0;
    }

    void TarNode::Close(){
        if(vol){
            vol->Close(this);
        }
    }
    int TarNode::ReadDir(struct fs_dirent* dirent, uint32_t index){
        if(vol){
            return vol->ReadDir(this, dirent, index);
        } else return -10;
    }
    FsNode* TarNode::FindDir(char* name){
        if(vol){
            return vol->FindDir(this, name);
        } else return nullptr;
    }

    void TarVolume::MakeNode(tar_header_t* header, TarNode* n, ino_t inode, ino_t parent, tar_header_t* dirHeader){
        n->parentInode = parent;
        n->header = header;

        n->inode = inode;
        n->uid = OctToDec(header->ustar.uid, 8);
        n->flags = TarTypeToFilesystemFlags(header->ustar.type);
        n->vol = this;

        char* name = header->ustar.name;
        char* _name = strtok(header->ustar.name, "/");
        while(_name){
            name = _name;
            _name = strtok(NULL, "/");
        }
        strcpy(n->name, name);
        n->size = GetSize(header->ustar.size);
    }

    int TarVolume::ReadDirectory(int blockIndex, ino_t parent){
        ino_t dirInode = nextNode++;
        tar_header_t* dirHeader = &blocks[blockIndex];
        TarNode* dirNode = &nodes[dirInode];
        MakeNode(dirHeader, dirNode, dirInode, parent);
        dirNode->entryCount = 0;

        int i = blockIndex + GetBlockCount(dirHeader->ustar.size) + 1; // Next block
        for(; i < blockCount; dirNode->entryCount++){
            if(strncmp(blocks[i].ustar.name, dirHeader->ustar.name, strlen(dirHeader->ustar.name))){
                break; // End of directory - header is not in directory
            }
            i += GetBlockCount(blocks[i].ustar.size) + 1;
        }
        
        dirNode->children = (ino_t*)kmalloc(sizeof(ino_t) * dirNode->entryCount);

        i = blockIndex + GetBlockCount(dirHeader->ustar.size) + 1;
        for(int e = 0; i < blockCount && e < dirNode->entryCount; e++){ // Iterate through directory
            ino_t inode = nextNode++;
            TarNode* n = &nodes[inode];
            MakeNode(&blocks[i], n, inode, dirInode, dirHeader);
            dirNode->children[e] = inode;
            //Log::Info("[TAR] Found File: %s, ", blocks[i].ustar.name);

            i += GetBlockCount(blocks[i].ustar.size) + 1;
        }

        return i;
    }

    TarVolume::TarVolume(uintptr_t base, size_t size, char* name){
        blocks = (tar_header_t*)base;
        blockCount = size / 512;

        Log::Info(" [TAR] Base: %x, Size: %x", base, size);

        int entryCount = 0;
        for(uint64_t i = 0; i < blockCount; i++, nodeCount++){ // Get file count
            tar_header_t header = blocks[i];

            if(!strchr(header.ustar.name, '/') || (header.ustar.type == TAR_TYPE_DIRECTORY && strchr(header.ustar.name, '/') == header.ustar.name + strlen(header.ustar.name) - 1)) entryCount++;

            i += GetBlockCount(header.ustar.size);
        }

        nodes = new TarNode[nodeCount];

        TarNode* volumeNode = &nodes[0];
        volumeNode->header = nullptr;
        volumeNode->flags = FS_NODE_DIRECTORY | FS_NODE_MOUNTPOINT;
        volumeNode->inode = 0;
        volumeNode->size = size;
        volumeNode->vol = this;
        volumeNode->parent = 0;

        mountPoint = volumeNode;
        strcpy(volumeNode->name,name);
        strcpy(mountPointDirent.name, volumeNode->name);
        mountPointDirent.type = FS_NODE_DIRECTORY;

        volumeNode->children = (ino_t*)kmalloc(sizeof(ino_t) * entryCount);
        volumeNode->entryCount = entryCount;
        int e = 0;
        for(int i = 0; i < blockCount; e++){
            tar_header_t header = blocks[i];

            if(header.ustar.type == TAR_TYPE_DIRECTORY){
                volumeNode->children[e] = nextNode; // Directory will take next available node so add it to children
                i = ReadDirectory(i, 0);
                continue;
            } else if(header.ustar.type == TAR_TYPE_FILE) {
                ino_t inode = nextNode++;
                TarNode* node = &nodes[inode];
                MakeNode(&blocks[i], node, inode, 0);
                volumeNode->children[e] = inode;
            }

            i += GetBlockCount(header.ustar.size);
            i++;
        }
        volumeNode->entryCount = e;
    }

    size_t TarVolume::Read(TarNode* node, size_t offset, size_t size, uint8_t *buffer){
        TarNode* tarNode = &nodes[node->inode];

		if(offset > node->size) return 0;
		else if(offset + size > node->size || size > node->size) size = node->size - offset;

		if(!size) return 0;

		memcpy(buffer, (void*)(((uintptr_t)tarNode->header) + 512 + offset), size);
		return size;
    }

    size_t TarVolume::Write(TarNode* node, size_t offset, size_t size, uint8_t *buffer){
        TarNode* tarNode = &nodes[node->inode];

        return 0;
    }

    void TarVolume::Open(TarNode* node, uint32_t flags){
        TarNode* tarNode = &nodes[node->inode];
    }

    void TarVolume::Close(TarNode* node){
        TarNode* tarNode = &nodes[node->inode];
    }

    int TarVolume::ReadDir(TarNode* node, struct fs_dirent* dirent, uint32_t index){
        TarNode* tarNode = &nodes[node->inode];
        
        if(!(node->flags & FS_NODE_DIRECTORY)) return -1;

        if(index >= tarNode->entryCount + 2) return -2;

        if(index == 0){
            strcpy(dirent->name, ".");
            return 0;
        } else if(index == 1){
            strcpy(dirent->name, "..");
            return 0;
        }

        TarNode* dir = &nodes[tarNode->children[index - 2]];

        strcpy(dirent->name, dir->name);
        dirent->type = dir->flags;
        dirent->inode = dir->inode;

        return 0;
    }

    FsNode* TarVolume::FindDir(TarNode* node, char* name){
        TarNode* tarNode = &nodes[node->inode];
        
        if(!(node->flags & FS_NODE_DIRECTORY)) return nullptr;

        if(strcmp(".", name) == 0) return node;
        if(strcmp("..", name) == 0){
            if(node->inode == 0) return fs::GetRoot();
            else return &nodes[tarNode->parentInode];
        }

        for(int i = 0; i < tarNode->entryCount; i++){
            if(strcmp(nodes[tarNode->children[i]].name, name) == 0) return &nodes[tarNode->children[i]];
        }

        return nullptr;
    }
}