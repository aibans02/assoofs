#include <linux/module.h>      /* Needed by all modules */
#include <linux/kernel.h>      /* Needed for KERN_INFO  */
#include <linux/init.h>        /* Needed for the macros */
#include <linux/fs.h>          /* libfs stuff           */
#include <linux/buffer_head.h> /* buffer_head           */
#include <linux/slab.h>        /* kmem_cache            */
#include "assoofs.h"

/*
 *  Operaciones sobre ficheros
 */
ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);
ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no)
{
    struct assoofs_inode_info *inode_info = NULL;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;
    struct buffer_head *bh;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;

    for (i = 0; i < afs_sb->inodes_count; i++)
    {
        if (inode_info->inode_no == inode_no)
        {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            memcpy(buffer, inode_info, sizeof(*buffer));
            break;
        }
        inode_info++;
    }

    brelse(bh);
    return buffer;
}

ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    printk(KERN_INFO "Read request\n");
    return -1;
}

ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
    printk(KERN_INFO "Write request\n");
    return -1;
}

/*
 *  Operaciones sobre directorios
 */
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = assoofs_iterate,
};

static int assoofs_iterate(struct file *filp, struct dir_context *ctx)
{
    struct inode *inode;
    struct super_block *sb;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    printk(KERN_INFO "Iterate request\n");

    if (ctx->pos)
        return 0;

    inode = filp->f_path.dentry->d_inode;
    sb = inode->i_sb;
    inode_info = inode->i_private;

    if ((!S_ISDIR(inode_info->mode)))
        return -1;

    bh = sb_bread(sb, inode_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    for (i = 0; i < inode_info->dir_children_count; i++)
    {
        dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);
        ctx->pos += sizeof(struct assoofs_dir_record_entry);
        record++;
    }

    brelse(bh);
    return 0;

    return 0;
}

/*
 *  Operaciones sobre inodos
 */
static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

static struct inode *assoofs_get_inode(struct super_block *sb, int ino);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_save_sb_info(struct super_block *vsb);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search);

static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
};

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    printk(KERN_INFO "Lookup request\n");

    bh = sb_bread(sb, parent_info->data_block_number);

    printk(KERN_INFO "Lookup in: ino=%llu, b=%llu\n", parent_info->inode_no, parent_info->data_block_number);

    record = (struct assoofs_dir_record_entry *)bh->b_data;

    for (i = 0; i < parent_info->dir_children_count; i++)
    {
        printk(KERN_INFO "Have file: '%s' (ino=%llu)\n", record->filename, record->inode_no);
        if (!strcmp(record->filename, child_dentry->d_name.name))
        {
            struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Función auxiliar que obtine la información de un inodo a partir de su número de inodo.
            inode_init_owner(inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
            d_add(child_dentry, inode);
            return NULL;
        }
        record++;
    }

    printk(KERN_ERR "No inode found for the filename [%s]\n", child_dentry->d_name.name);

    return NULL;
}

static int assoofs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct inode *inode;
    struct buffer_head *bh;
    uint64_t count;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;
    struct assoofs_inode_info *inode_info;
    struct super_block *sb;

    printk(KERN_INFO "New file request\n");

    sb = dir->i_sb;                                                           // obtengo un puntero al superbloque desde dir
    count = ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count; // obtengo el número de inodos de la información persistente del superbloque

    if (count < 0)
    {
        return count;
    }

    if (unlikely(count >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED))
    {
        /* The above condition can be just == insted of the >= */
        printk(KERN_ERR
               "Maximum number of objects supported by assoofs is already reached");
        return -ENOSPC;
    }

    if (!S_ISDIR(mode) && !S_ISREG(mode))
    {
        printk(KERN_ERR
               "Creation request but for neither a file nor a directory");
        return -EINVAL;
    }

    inode = new_inode(sb);
    //TODO Comprobacion de si count > ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED
    inode->i_ino = (count + 1); // Asigno número al nuevo inodo a partir de count

    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = mode; // El segundo mode me llega como argumento
    inode_info->file_size = 0;
    inode->i_private = inode_info;
    inode_init_owner(inode, dir, mode);
    d_add(dentry, inode);

    inode->i_fop = &assoofs_file_operations;
    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);

    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);

    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;
    dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.

    strcpy(dir_contents->filename, dentry->d_name.name);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    return 0;
}

static int assoofs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    printk(KERN_INFO "New directory request\n");
    return 0;
}

static struct inode *assoofs_get_inode(struct super_block *sb, int ino)
{
    struct inode *inode;
    struct assoofs_inode_info *inode_info;

    printk(KERN_INFO "assoofs_get_inode\n");

    inode_info = assoofs_get_inode_info(sb, ino);

    inode = new_inode(sb);
    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;

    if (S_ISDIR(inode_info->mode))
        inode->i_fop = &assoofs_dir_operations;
    else if (S_ISREG(inode_info->mode))
        inode->i_fop = &assoofs_file_operations;
    else
        printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");

    inode->i_atime = inode->i_mtime = inode->i_ctime =
        current_time(inode);

    inode->i_private = inode_info;

    return inode;
}

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;

    printk(KERN_INFO "assoofs_sb_get_a_freeblock\n");

    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
        if (assoofs_sb->free_blocks & (1 << i))
            break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posición del primer bloque libre

    if (unlikely(i == ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED))
    {
        printk(KERN_ERR "No more free blocks available");
        return -ENOSPC;
    }

    *block = i; // Escribimos el valor de i en la dirección de memoria indicada como segundo argumento en la función

    assoofs_sb->free_blocks &= ~(1 << i);

    assoofs_save_sb_info(sb);

    return 0;
}

void assoofs_save_sb_info(struct super_block *vsb)
{
    struct buffer_head *bh;
    struct assoofs_super_block_info *sb = vsb->s_fs_info; // Información persistente del superbloque en memoria

    printk(KERN_INFO "assoofs_save_sb_info\n");

    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    struct buffer_head *bh = NULL;
    struct assoofs_inode_info *inode_info = NULL;

    printk(KERN_INFO "assoofs_add_inode_info\n");

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

    inode_info = (struct assoofs_inode_info *)bh->b_data;
    inode_info += assoofs_sb->inodes_count;
    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);

    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);
}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
    struct assoofs_inode_info *inode_pos;
    struct buffer_head *bh;

    printk(KERN_INFO "assoofs_save_inode_info\n");

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);

    if (likely(inode_pos))
    {
        memcpy(inode_pos, inode_info, sizeof(*inode_pos));
        printk(KERN_INFO "The inode updated\n");

        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
    }
    else
    {
        printk(KERN_ERR
               "The new filesize could not be stored to the inode.");
        return -EIO;
    }

    brelse(bh);

    return 0;
}

struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
    uint64_t count = 0;

    printk(KERN_INFO "assoofs_search_inode_info\n");

    while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count)
    {
        count++;
        start++;
    }
    if (start->inode_no == search->inode_no)
        return start;
    else
        return NULL;
}

/*
 *  Operaciones sobre el superbloque
 */
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent)
{
    // 1.- Leer la información persistente del superbloque del dispositivo de bloques
    // 2.- Comprobar los parámetros del superbloque
    // 3.- Escribir la información persistente leída del dispositivo de bloques en el superbloque sb, incluído el campo s_op con las operaciones que soporta.
    // 4.- Crear el inodo raíz y asignarle operaciones sobre inodos (i_op) y sobre directorios (i_fop)

    struct inode *root_inode;
    struct buffer_head *bh;
    struct assoofs_super_block_info *sb_disk;

    printk(KERN_INFO "assoofs_fill_super request\n");

    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);

    sb_disk = (struct assoofs_super_block_info *)bh->b_data;

    printk(KERN_INFO "The magic number obtained in disk is: [%llu]\n",
           sb_disk->magic);

    if (unlikely(sb_disk->magic != ASSOOFS_MAGIC))
    {
        printk(KERN_ERR
               "The filesystem that you try to mount is not of type assoofs. Magicnumber mismatch.");
        brelse(bh);
    }

    if (unlikely(sb_disk->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE))
    {
        printk(KERN_ERR
               "assofs seem to be formatted using a non-standard block size.");
        brelse(bh);
    }

    printk(KERN_INFO
           "assoofs filesystem of version [%llu] formatted with a block size of [%llu] detected in the device.\n",
           sb_disk->version, sb_disk->block_size);

    sb->s_magic = ASSOOFS_MAGIC;

    sb->s_fs_info = sb_disk;

    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    sb->s_op = &assoofs_sops;

    root_inode = new_inode(sb);
    inode_init_owner(root_inode, NULL, S_IFDIR);
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
    root_inode->i_sb = sb;
    root_inode->i_op = &assoofs_inode_ops;
    root_inode->i_fop = &assoofs_dir_operations;
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
        current_time(root_inode);

    root_inode->i_private =
        assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
    {
        brelse(bh);
        return -ENOMEM;
    }

    brelse(bh);
    return 0;
}

/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    struct dentry *ret;

    printk(KERN_INFO "assoofs_mount request\n");

    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk(KERN_ERR "Error mounting assoofs");
    else
        printk(KERN_INFO "assoofs is succesfully mounted on [%s]\n",
               dev_name);

    return ret;
}

static void assoofs_kill_superblock(struct super_block *sb)
{
    printk(KERN_INFO
           "assoos superblock is destroyed. Unmount succesful.\n");
    /* This is just a dummy function as of now. As our filesystem gets matured,
	 * we will do more meaningful operations here */

    kill_block_super(sb);
    return;
}
/*
 *  assoofs file system type
 */
static struct file_system_type assoofs_type = {
    .owner = THIS_MODULE,
    .name = "assoofs",
    .mount = assoofs_mount,
    .kill_sb = assoofs_kill_superblock,
};

static int __init assoofs_init(void)
{
    int ret;
    printk(KERN_INFO "assoofs_init request\n");
    ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret
    if (likely(ret == 0))
        printk(KERN_INFO "Sucessfully registered assoofs\n");
    else
        printk(KERN_ERR "Failed to register assoofs. Error:[%d]", ret);

    return ret;
}

static void __exit assoofs_exit(void)
{
    int ret;
    printk(KERN_INFO "assoofs_exit request\n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de ret

    if (likely(ret == 0))
        printk(KERN_INFO "Sucessfully unregistered assoofs\n");
    else
        printk(KERN_ERR "Failed to unregister assoofs. Error:[%d]",
               ret);
}

module_init(assoofs_init);
module_exit(assoofs_exit);
