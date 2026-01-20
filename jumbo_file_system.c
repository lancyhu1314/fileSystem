#include "jumbo_file_system.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// C does not have a bool type, so I created one that you can use
typedef char bool_t;
#define TRUE 1
#define FALSE 0

// Directory or file
#define IS_DIR 0
#define IS_FILE 1

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static block_num_t current_dir;

// optional helper function you can implement to tell you if a block is a dir node or an inode
static bool_t is_dir(block_num_t block_num)
{
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));

  // Read the block from disk
  if (read_block(block_num, &current_block) < 0)
  {
    return FALSE;
  }

  // Check if the block represents a directory node
  if (current_block.is_dir == IS_DIR)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

/* jfs_mount
 *   prepares the DISK file on the _real_ file system to have file system
 *   blocks read and written to it.  The application _must_ call this function
 *   exactly once before calling any other jfs_* functions.  If your code
 *   requires any additional one-time initialization before any other jfs_*
 *   functions are called, you can add it here.
 * filename - the name of the DISK file on the _real_ file system
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_mount(const char *filename)
{
  int ret = bfs_mount(filename);
  if (ret < 0)
  {
    printf("Error: Unable to mount the basic file system.\n");
    return E_UNKNOWN;
  }

  current_dir = 1;
  return ret;
}

/* jfs_mkdir
 *   creates a new subdirectory in the current directory
 * directory_name - name of the new subdirectory
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_mkdir(const char *directory_name)
{
  uint16_t num_entries = 0;
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Directory_name already exists
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, directory_name) == 0)
    {
      return E_EXISTS;
    }
    num_entries++;
  }

  // Directory_name exceeds maximum length
  if (strlen(directory_name) > MAX_NAME_LENGTH)
  {
    return E_MAX_NAME_LENGTH;
  }

  // Current directory reach maximum number
  if (num_entries >= MAX_DIR_ENTRIES)
  {
    return E_MAX_DIR_ENTRIES;
  }

  // Allocate new block for new directory
  block_num_t new_dir_block_num = allocate_block();
  if (new_dir_block_num == 0)
  {
    return E_DISK_FULL;
  }

  // Initialize new directory block
  struct block new_dir_block;
  memset(&new_dir_block, 0, sizeof(struct block));
  new_dir_block.is_dir = IS_DIR;
  new_dir_block.contents.dirnode.num_entries = 0;

  // Write disk
  if (write_block(new_dir_block_num, &new_dir_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Add to current directory
  strcpy(current_block.contents.dirnode.entries[num_entries].name, directory_name);
  current_block.contents.dirnode.entries[num_entries].block_num = new_dir_block_num;
  current_block.contents.dirnode.num_entries++;

  // Update current directory block
  if (write_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  return E_SUCCESS;
}

/* jfs_chdir
 *   changes the current directory to the specified subdirectory, or changes
 *   the current directory to the root directory if the directory_name is NULL
 * directory_name - name of the subdirectory to make the current
 *   directory; if directory_name is NULL then the current directory
 *   should be made the root directory instead
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR
 */
int jfs_chdir(const char *directory_name)
{
  block_num_t current_block_num = current_dir;

  // Change current directory to root directory
  if (directory_name == NULL)
  {
    current_dir = 1;
    return E_SUCCESS;
  }

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the specified subdirectory
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    block_num_t block_num = current_block.contents.dirnode.entries[i].block_num;
    if (strcmp(current_block.contents.dirnode.entries[i].name, directory_name) == 0)
    {
      // Check if entry is a directory
      if (!is_dir(block_num))
      {
        return E_NOT_DIR;
      }
      // Change current directory to the specified subdirectory
      current_dir = block_num;
      return E_SUCCESS;
    }
  }

  return E_NOT_EXISTS;
}

/* jfs_ls
 *   finds the names of all the files and directories in the current directory
 *   and writes the directory names to the directories argument and the file
 *   names to the files argument
 * directories - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * file - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * returns 0 on success or one of the following error codes on failure:
 *   (this function should always succeed)
 */
int jfs_ls(char *directories[MAX_DIR_ENTRIES + 1], char *files[MAX_DIR_ENTRIES + 1])
{
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  int dir_count = 0;
  int file_count = 0;

  // Iterate through directory entries
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    block_num_t block_num = current_block.contents.dirnode.entries[i].block_num;
    char *name = current_block.contents.dirnode.entries[i].name;
    if (is_dir(block_num))
    {
      // For directory, caller will free
      directories[dir_count] = (char *)malloc((strlen(name) + 1) * sizeof(char));
      strcpy(directories[dir_count], name);
      dir_count++;
    }
    else
    {
      // For file, caller will free
      files[file_count] = (char *)malloc((strlen(name) + 1) * sizeof(char));
      strcpy(files[file_count], name);
      file_count++;
    }
  }

  // Terminate the arrays with NULL pointers
  directories[dir_count] = NULL;
  files[file_count] = NULL;

  return E_SUCCESS;
}

/* jfs_rmdir
 *   removes the specified subdirectory of the current directory
 * directory_name - name of the subdirectory to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR, E_NOT_EMPTY
 */
int jfs_rmdir(const char *directory_name)
{
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the specified subdirectory
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    block_num_t block_num = current_block.contents.dirnode.entries[i].block_num;
    if (strcmp(current_block.contents.dirnode.entries[i].name, directory_name) == 0)
    {
      // Check if entry is a directory
      if (!is_dir(block_num))
      {
        return E_NOT_DIR;
      }

      // Check if directory is empty
      struct block dir_block;
      memset(&dir_block, 0, sizeof(struct block));
      if (read_block(block_num, &dir_block) < 0)
      {
        return E_UNKNOWN;
      }

      // Directory is not empty
      if (dir_block.contents.dirnode.num_entries > 0)
      {
        return E_NOT_EMPTY;
      }

      // Release directory block and remove directory entry
      if (release_block(block_num) < 0)
      {
        return E_UNKNOWN;
      }

      // Remove and reset directory entry
      for (int j = i; j < current_block.contents.dirnode.num_entries - 1; j++)
      {
        current_block.contents.dirnode.entries[j] = current_block.contents.dirnode.entries[j + 1];
      }
      current_block.contents.dirnode.num_entries--;

      // update current directory's block
      if (write_block(current_block_num, &current_block) < 0)
      {
        return E_UNKNOWN;
      }
      return E_SUCCESS;
    }
  }

  return E_NOT_EXISTS;
}

/* jfs_creat
 *   creates a new, empty file with the specified name
 * file_name - name to give the new file
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_creat(const char *file_name)
{
  block_num_t current_block_num = current_dir;

  // Check file name length
  if (strlen(file_name) > MAX_NAME_LENGTH)
  {
    return E_MAX_NAME_LENGTH;
  }

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Check if file already exists
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, file_name) == 0)
    {
      return E_EXISTS;
    }
  }

  // Check maximum
  if (current_block.contents.dirnode.num_entries >= MAX_DIR_ENTRIES)
  {
    return E_MAX_DIR_ENTRIES;
  }

  // Allocate a new block for the file's inode
  block_num_t inode_block_num = allocate_block();
  if (inode_block_num == 0)
  {
    return E_DISK_FULL;
  }

  // Initialize the new inode block
  struct block inode_block;
  memset(&inode_block, 0, sizeof(struct block));
  inode_block.is_dir = IS_FILE;
  inode_block.contents.inode.file_size = 0;
  memset(inode_block.contents.inode.data_blocks, 0, sizeof(inode_block.contents.inode.data_blocks));

  // Write the initialized inode block to disk
  if (write_block(inode_block_num, &inode_block) < 0)
  {
    // Release allocated block if fault
    release_block(inode_block_num);
    return E_UNKNOWN;
  }

  // Add new file entry to the current directory
  current_block.contents.dirnode.entries[current_block.contents.dirnode.num_entries].block_num = inode_block_num;
  strcpy(current_block.contents.dirnode.entries[current_block.contents.dirnode.num_entries].name, file_name);
  current_block.contents.dirnode.num_entries++;

  // Updated current directory's block
  if (write_block(current_block_num, &current_block) < 0)
  {
    // Release allocated block if fault
    release_block(inode_block_num);
    return E_UNKNOWN;
  }

  return E_SUCCESS;
}

/* jfs_remove
 *   deletes the specified file and all its data (note that this cannot delete
 *   directories; use rmdir instead to remove directories)
 * file_name - name of the file to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */
int jfs_remove(const char *file_name)
{
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the file
  int file_index = -1;
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, file_name) == 0)
    {
      file_index = i;
      break;
    }
  }

  // File not found
  if (file_index == -1)
  {
    return E_NOT_EXISTS;
  }

  // Get the block number of the inode
  block_num_t inode_block_num = current_block.contents.dirnode.entries[file_index].block_num;

  // Read the inode block of the file
  struct block inode_block;
  memset(&inode_block, 0, sizeof(struct block));
  if (read_block(inode_block_num, &inode_block) < 0)
  {
    return E_UNKNOWN;
  }

  // If not a file
  if (inode_block.is_dir == IS_DIR)
  {
    return E_IS_DIR;
  }

  // Release all data blocks allocated to the file
  for (uint32_t i = 0; i < MAX_DATA_BLOCKS && inode_block.contents.inode.data_blocks[i] != 0; i++)
  {
    if (release_block(inode_block.contents.inode.data_blocks[i]) < 0)
    {
      return E_UNKNOWN;
    }
  }

  // Release the inode block of the file
  if (release_block(inode_block_num) < 0)
  {
    return E_UNKNOWN;
  }

  // Remove the file entry from the current directory
  for (int i = file_index; i < current_block.contents.dirnode.num_entries - 1; i++)
  {
    current_block.contents.dirnode.entries[i] = current_block.contents.dirnode.entries[i + 1];
  }
  current_block.contents.dirnode.num_entries--;

  // Updated current directory's block
  if (write_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  return E_SUCCESS;
}

/* jfs_stat
 *   returns the file or directory stats (see struct stat for details)
 * name - name of the file or directory to inspect
 * buf  - pointer to a struct stat (already allocated by the caller) where the
 *   stats will be written
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS
 */
int jfs_stat(const char *name, struct stats *buf)
{
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the file or directory in the current directory
  int file_index = -1;
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, name) == 0)
    {
      file_index = i;
      break;
    }
  }

  // File or directory not found
  if (file_index == -1)
  {
    return E_NOT_EXISTS;
  }

  // Get the block number of the entry
  block_num_t entry_block_num = current_block.contents.dirnode.entries[file_index].block_num;

  // Fill in stats struct
  buf->block_num = entry_block_num;
  strcpy(buf->name, name);

  // Read the entry block to determine if it's a directory or a file
  struct block entry_block;
  memset(&entry_block, 0, sizeof(struct block));
  if (read_block(entry_block_num, &entry_block) < 0)
  {
    return E_UNKNOWN;
  }

  if (entry_block.is_dir == IS_DIR)
  {
    // Directory
    buf->is_dir = IS_DIR;
    buf->num_data_blocks = 0;
    buf->file_size = 0;
  }
  else
  {
    // File
    buf->is_dir = IS_FILE;
    buf->num_data_blocks = entry_block.contents.inode.file_size > 0 ? (entry_block.contents.inode.file_size - 1) / BLOCK_SIZE + 1 : 0;
    buf->file_size = entry_block.contents.inode.file_size;
  }

  return E_SUCCESS;
}

int jfs_write(const char *file_name, const void *buf, unsigned short count)
{
  block_num_t current_block_num = current_dir;
  unsigned short write_size = count;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the file
  int file_index = -1;
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, file_name) == 0)
    {
      file_index = i;
      break;
    }
  }

  // File not found
  if (file_index == -1)
  {
    return E_NOT_EXISTS;
  }

  // Read the inode block of the file
  struct block inode_block;
  memset(&inode_block, 0, sizeof(struct block));
  if (read_block(current_block.contents.dirnode.entries[file_index].block_num, &inode_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Check if the file is a directory
  if (inode_block.is_dir == IS_DIR)
  {
    return E_IS_DIR;
  }

  // Exceed maximum file size after writing
  if (inode_block.contents.inode.file_size + write_size > MAX_FILE_SIZE)
  {
    return E_MAX_FILE_SIZE;
  }

  // Calculate the start position in the last block for appending data
  unsigned short append_position = inode_block.contents.inode.file_size % BLOCK_SIZE;

  // Calculate last block's number
  unsigned short last_block_num = (inode_block.contents.inode.file_size - 1) / BLOCK_SIZE;

  // Create new buf to combine and store previous buf
  uint8_t *append_buffer = NULL;
  if (append_position > 0 && inode_block.contents.inode.file_size > 0)
  {
    append_buffer = (uint8_t *)malloc(MAX_FILE_SIZE * sizeof(uint8_t));
    if (append_buffer == NULL)
    {
      return E_UNKNOWN;
    }
    memset(append_buffer, 0, MAX_FILE_SIZE);

    if (read_block(inode_block.contents.inode.data_blocks[last_block_num], append_buffer) < 0)
    {
      free(append_buffer);
      return E_UNKNOWN;
    }
    memcpy(append_buffer + append_position, buf, write_size);

    // release last block for future recreate
    release_block(inode_block.contents.inode.data_blocks[last_block_num]);
  }
  else
  {
    // Used up the last block before or first time create, the append_buffer equals buf
    append_buffer = (uint8_t *)buf;
  }

  // update new length for count
  write_size += append_position;

  // Calculate the number of blocks needed to write the data
  unsigned short blocks_needed = (write_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

  // Calculate position to create new block
  unsigned short next_start_position = (inode_block.contents.inode.file_size + BLOCK_SIZE - 1) / BLOCK_SIZE - 1;
  if (inode_block.contents.inode.file_size == 0)
    next_start_position = 0;
  else if (append_position == 0)
    next_start_position = inode_block.contents.inode.file_size / BLOCK_SIZE;

  // Allocate data blocks for the file
  block_num_t data_block_nums[blocks_needed];
  for (int i = next_start_position; i < next_start_position + blocks_needed; i++)
  {
    data_block_nums[i] = allocate_block();
    if (data_block_nums[i] == 0)
    {
      // if not success, release previous created block
      for (int j = next_start_position; j < i; j++)
      {
        release_block(data_block_nums[j]);
      }
      if (append_position > 0 && inode_block.contents.inode.file_size > 0)
      {
        free(append_buffer);
      }
      return E_DISK_FULL;
    }
  }

  // Write data to the allocated data blocks
  unsigned short bytes_written = 0;
  for (int i = next_start_position; i < next_start_position + blocks_needed; i++)
  {
    unsigned short bytes_to_write = MIN(write_size - bytes_written, BLOCK_SIZE);
    // Create a temporary buffer to hold the data to be written
    uint8_t *buffer_to_write = (uint8_t *)malloc(BLOCK_SIZE * sizeof(uint8_t));
    if (buffer_to_write == NULL)
    {
      // free allocated blocks
      for (int j = next_start_position; j < i; j++)
      {
        release_block(data_block_nums[j]);
      }
      if (append_position > 0 && inode_block.contents.inode.file_size > 0)
      {
        free(append_buffer);
      }
      return E_UNKNOWN;
    }
    memset(buffer_to_write, 0, BLOCK_SIZE);
    memcpy(buffer_to_write, append_buffer + bytes_written, bytes_to_write);

    // Write the data from the temporary buffer to the block
    if (write_block(data_block_nums[i], buffer_to_write) < 0)
    {
      free(buffer_to_write);
      // free allocated blocks
      for (int j = next_start_position; j < i; j++)
      {
        release_block(data_block_nums[j]);
      }
      if (append_position > 0 && inode_block.contents.inode.file_size > 0)
      {
        free(append_buffer);
      }
      return E_UNKNOWN;
    }
    bytes_written += bytes_to_write;
    free(buffer_to_write);
  }

  // Update inode block with new data block numbers and file size
  for (int i = next_start_position; i < next_start_position + blocks_needed; i++)
  {
    inode_block.contents.inode.data_blocks[i] = data_block_nums[i];
  }
  inode_block.contents.inode.file_size += (write_size - append_position);

  // Updated inode block
  if (write_block(current_block.contents.dirnode.entries[file_index].block_num, &inode_block) < 0)
  {
    if (append_position > 0 && inode_block.contents.inode.file_size > 0)
    {
      free(append_buffer);
    }
    return E_UNKNOWN;
  }

  if (append_position > 0 && inode_block.contents.inode.file_size > 0)
  {
    free(append_buffer);
  }

  return E_SUCCESS;
}

/* jfs_read
 *   reads the specified file and copies its contents into the buffer, up to a
 *   maximum of *ptr_count bytes copied (but obviously no more than the file
 *   size, either)
 * file_name - name of the file to read
 * buf - buffer where the file data should be written

 *   contains the size of buf when it's passed in, and will be modified to
 *   contain the number of bytes actually written to buf (e.g., if the file is
 *   smaller than the buffer) if this function is successful
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */

int jfs_read(const char *file_name, void *buf, unsigned short *ptr_count)
{
  block_num_t current_block_num = current_dir;

  // Read current directory's block
  struct block current_block;
  memset(&current_block, 0, sizeof(struct block));
  if (read_block(current_block_num, &current_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Search for the file in the current directory
  int file_index = -1;
  for (int i = 0; i < current_block.contents.dirnode.num_entries; i++)
  {
    if (strcmp(current_block.contents.dirnode.entries[i].name, file_name) == 0)
    {
      file_index = i;
      break;
    }
  }

  // File not found
  if (file_index == -1)
  {
    return E_NOT_EXISTS;
  }

  // Read the inode block of the file
  struct block inode_block;
  if (read_block(current_block.contents.dirnode.entries[file_index].block_num, &inode_block) < 0)
  {
    return E_UNKNOWN;
  }

  // Check if the file is a directory
  if (inode_block.is_dir == IS_DIR)
  {
    return E_IS_DIR;
  }

  // Calculate the number of bytes to read
  unsigned short bytes_to_read = MIN(inode_block.contents.inode.file_size, *ptr_count);

  // How many block have used
  unsigned short block_used = (bytes_to_read + BLOCK_SIZE - 1) / BLOCK_SIZE;

  // Read file data from data blocks
  unsigned short bytes_have_read = 0;

  // store each block's data
  uint8_t *current_block_buf = (uint8_t *)malloc(BLOCK_SIZE * sizeof(uint8_t));
  if (current_block_buf == NULL)
  {
    return E_UNKNOWN;
  }

  for (int i = 0; i < block_used; i++)
  {
    // Calculate the number of bytes to read from this block
    unsigned short bytes_this_block = MIN(bytes_to_read - bytes_have_read, BLOCK_SIZE);

    // Read data from block
    if (read_block(inode_block.contents.inode.data_blocks[i], current_block_buf) < 0)
    {
      free(current_block_buf);
      return E_UNKNOWN;
    }

    // append each block's data to the buf
    memcpy((uint8_t *)buf + bytes_have_read, current_block_buf, bytes_this_block);

    bytes_have_read += bytes_this_block;
  }

  // Update ptr_count to show the number of bytes actually read
  *ptr_count = bytes_have_read;

  free(current_block_buf);

  return E_SUCCESS;
}

/* jfs_unmount
 *   makes the file system no longer accessible (unless it is mounted again).
 *   This should be called exactly once after all other jfs_* operations are
 *   complete; it is invalid to call any other jfs_* function (except
 *   jfs_mount) after this function complete.  Basically, this closes the DISK
 *   file on the _real_ file system.  If your code requires any clean up after
 *   all other jfs_* functions are done, you may add it here.
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_unmount()
{
  int ret = bfs_unmount();
  return ret;
}
