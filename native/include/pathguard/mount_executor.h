#pragma once

namespace pathguard {

int BindMountDirectoryFds(int source_fd, int target_fd);
int MoveMountDirectoryFds(int source_fd, int target_fd);
int MountDenyDirectoryFd(int target_fd);
int UnmountDirectoryFd(int target_fd);

}  // namespace pathguard
