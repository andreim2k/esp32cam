/**
 * Helper to convert IPAddress to char array safely
 */
void ipToCharArray(const IPAddress& ip, char* buffer, size_t buf_size) {
  ip.toString().toCharArray(buffer, buf_size);
}