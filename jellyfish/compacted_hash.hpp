#ifndef __JELLYFISH_COMPACTED_HASH__
#define __JELLYFISH_COMPACTED_HASH__

#include <iostream>
#include <fstream>
#include <string.h>
#include <pthread.h>
#include <jellyfish/mapped_file.hpp>
#include <jellyfish/square_binary_matrix.hpp>
#include <jellyfish/fasta_parser.hpp>

namespace jellyfish {
  namespace compacted_hash {
    class ErrorReading : public std::exception {
      std::string msg;
    public:
      ErrorReading(const std::string _msg) : msg(_msg) { }
      virtual ~ErrorReading() throw() {}
      virtual const char* what() const throw() {
        return msg.c_str();
      }
    };

    static const char *file_type = "JFLISTDN";
    struct header {
      char     type[8];         // type of file. Expect file_type
      uint64_t key_len;
      uint64_t val_len;         // In bytes
      uint64_t size;            // In bytes
      uint64_t max_reprobe;
      uint64_t unique;
      uint64_t distinct;
      uint64_t total;
      uint64_t max_count;

      header() { }
      header(char *ptr) {
        if(memcmp(ptr, file_type, sizeof(file_type)))
          throw new ErrorReading("Bad file type");
        memcpy((void *)this, ptr, sizeof(struct header));
      }
    };

    template<typename storage_t>
    class writer {
      uint64_t   unique, distinct, total, max_count;
      size_t     nb_records;
      uint_t     klen, vlen;
      uint_t     key_len, val_len;
      storage_t *ary;
      char      *buffer, *end, *ptr;

    public:
      writer() : unique(0), distinct(0), total(0), max_count(0)
      { buffer = ptr = end = NULL; }

      writer(size_t _nb_records, uint_t _klen, uint_t _vlen, storage_t *_ary)
      { 
        initialize(_nb_records, _klen, _vlen, _ary);
      }

      void initialize(size_t _nb_records, uint_t _klen, uint_t _vlen, storage_t *_ary) {
        unique     = distinct = total = max_count = 0;
        nb_records = _nb_records;
        klen       = _klen;
        vlen       = _vlen;
        key_len    = bits_to_bytes(klen);
        val_len    = bits_to_bytes(vlen);
        ary        = _ary;
        buffer     = new char[nb_records * (key_len + val_len)];
        end        = buffer + (nb_records * (key_len + val_len));
        ptr        = buffer;
      }

      ~writer() {
        if(buffer)
          delete buffer;
      }

      bool append(uint64_t key, uint64_t val) {
        if(ptr >= end)
          return false;
        memcpy(ptr, &key, key_len);
        ptr += key_len;
        memcpy(ptr, &val, val_len);
        ptr += val_len;
        unique += val == 1;
        distinct++;
        total += val;
        if(val > max_count)
          max_count = val;
        return true;
      }

      void dump(std::ostream *out) {
        out->write(buffer, ptr - buffer);
        ptr = buffer;
      }

      void write_header(std::ostream *out) const {
        struct header head;
        memset(&head, '\0', sizeof(head));
        memcpy(&head.type, file_type, sizeof(file_type));
        head.key_len = klen;
        head.val_len = val_len;
        head.size = ary->get_size();
        head.max_reprobe = ary->get_max_reprobe_offset();
        out->write((char *)&head, sizeof(head));
        ary->write_ary_header(out);
      }

      void update_stats(std::ostream *out) const {
        update_stats_with(out, unique, distinct, total, max_count);
      }

      void update_stats_with(std::ostream *out, uint64_t _unique, uint64_t _distinct,
                             uint64_t _total, uint64_t _max_count) const {
        struct header head;
        memcpy(&head.type, file_type, sizeof(file_type));
        head.key_len     = klen;
        head.val_len     = val_len;
        head.size        = ary->get_size();
        head.max_reprobe = ary->get_max_reprobe_offset();
        head.unique      = _unique;
        head.distinct    = _distinct;
        head.total       = _total;
        head.max_count   = _max_count;
        out->seekp(0);
        out->write((char *)&head, sizeof(head));
      }

      uint64_t get_unique() const { return unique; }
      uint64_t get_distinct() const { return distinct; }
      uint64_t get_total() const { return total; }
      uint64_t get_max_count() const { return max_count; }
      uint_t   get_key_len_bytes() const { return key_len; }
      uint_t   get_val_len_bytes() const { return val_len; }

      void reset_counters() {
        unique = distinct = total = max_count = 0;
      }
    };
    
    template<typename key_t, typename val_t>
    class reader {
      struct header              header;
      std::ifstream             *io;
      uint_t                     key_len;
      SquareBinaryMatrix         hash_matrix, hash_inverse_matrix;
      size_t                     record_len, buffer_len;
      size_t                     size_mask;
      char                      *buffer, *end_buffer, *ptr;

    public:
      key_t key;
      val_t val;

      reader() { io = 0; buffer = 0; }
      reader(std::string filename, size_t _buff_len = 10000000UL) { 
        initialize(filename, _buff_len);
      }

      void initialize(std::string filename, size_t _buff_len) {
        io = new ifstream(filename.c_str());
        io->read((char *)&header, sizeof(header));
        if(!io->good())
          throw new ErrorReading("Error reading header");
        if(memcmp(header.type, file_type, sizeof(file_type))) {
          throw new ErrorReading("Bad file type");
        }
        key_len  = (header.key_len / 8) + (header.key_len % 8 != 0);
        record_len = key_len + header.val_len;
        buffer_len = record_len * (_buff_len / record_len);
        buffer = new char[buffer_len];
        ptr = buffer;
        end_buffer = NULL;

        hash_matrix.load(io);
        hash_inverse_matrix.load(io);
        key = val = 0;
        size_mask = header.size - 1; //TODO: check that header.size is a power of 2
      }

      ~reader() {
        if(io)
          delete io;
        if(buffer)
          delete[] buffer;
      }

      uint_t get_key_len() const { return header.key_len; }
      uint_t get_mer_len() const { return header.key_len / 2; }
      uint_t get_val_len() const { return header.val_len; }
      size_t get_size() const { return header.size; }
      uint64_t get_max_reprobe() const { return header.max_reprobe; }
      uint64_t get_max_reprobe_offset() const { return header.max_reprobe; }
      uint64_t get_unique() const { return header.unique; }
      uint64_t get_distinct() const { return header.distinct; }
      uint64_t get_total() const { return header.total; }
      uint64_t get_max_count() const { return header.max_count; }
      SquareBinaryMatrix get_hash_matrix() const { return hash_matrix; }
      SquareBinaryMatrix get_hash_inverse_matrix() const { return hash_inverse_matrix; }
      void write_ary_header(std::ostream *out) const {
        hash_matrix.dump(out);
        hash_inverse_matrix.dump(out);
      }

      void get_string(char *out) const {
        fasta_parser::mer_binary_to_string(key, get_mer_len(), out);
      }
      uint64_t get_hash() const { return hash_matrix.times(key); }
      uint64_t get_pos() const { return hash_matrix.times(key) & size_mask; }

      bool next() {
        while(true) {
          if(ptr <= end_buffer) {
            memcpy(&key, ptr, key_len);
            ptr += key_len;
            memcpy(&val, ptr, header.val_len);
            ptr += header.val_len;
            return true;
          }

          if(io->fail())
            return false;
          io->read(buffer, buffer_len);
          //      if(record_len * (io->gcount() / record_len) != io->gcount())
          //        return false;
          ptr = buffer;
          end_buffer = NULL;
          if((typeof record_len)io->gcount() >= record_len)
            end_buffer = ptr + (io->gcount() - record_len);
        }
      }
    };

    template<typename key_t, typename val_t>
    class query {
      mapped_file         file;
      struct header       header;
      uint_t              key_len;
      uint_t              val_len;
      uint_t              record_len;
      SquareBinaryMatrix  hash_matrix;
      SquareBinaryMatrix  hash_inverse_matrix;
      char               *base;
      uint64_t            size;
      uint64_t            size_mask;
      uint64_t            last_id;
      key_t               first_key, last_key;
      uint64_t            first_pos, last_pos;
      bool                canonical;

    public:
      query(std::string filename) : 
        file(filename.c_str()), 
        header(file.base()), 
        key_len((header.key_len / 8) + (header.key_len % 8 != 0)),
        val_len(header.val_len),
        record_len(key_len + header.val_len),
        hash_matrix(file.base() + sizeof(header)),
        hash_inverse_matrix(file.base() + sizeof(header) + hash_matrix.dump_size()),
        base(file.base() + sizeof(header) + hash_matrix.dump_size() + hash_inverse_matrix.dump_size()),
        size(header.size),
        size_mask(header.size - 1),
        last_id((file.end() - base) / record_len),
        canonical(false)
      { 
        get_key(0, &first_key);
        first_pos = get_pos(first_key);
        get_key(last_id - 1, &last_key);
        last_pos = get_pos(last_key);
      }

      uint_t get_key_len() const { return header.key_len; }
      uint_t get_mer_len() const { return header.key_len / 2; }
      uint_t get_val_len() const { return header.val_len; }
      size_t get_size() const { return header.size; }
      uint64_t get_max_reprobe() const { return header.max_reprobe; }
      uint64_t get_max_reprobe_offset() const { return header.max_reprobe; }
      uint64_t get_unique() const { return header.unique; }
      uint64_t get_distinct() const { return header.distinct; }
      uint64_t get_total() const { return header.total; }
      uint64_t get_max_count() const { return header.max_count; }
      SquareBinaryMatrix get_hash_matrix() const { return hash_matrix; }
      SquareBinaryMatrix get_hash_inverse_matrix() const { return hash_inverse_matrix; }
      bool get_canonical() const { return canonical; }
      void set_canonical(bool v) { canonical = v; }

      /* No check is made on the validity of the string passed. Should only contained [acgtACGT] to get a valid answer.
       */
      val_t operator[] (const char *key_s) {
        return get_key_val(fasta_parser::mer_string_to_binary(key_s, get_mer_len()));
      }
      val_t operator[] (key_t key) { return get_key_val(key); }

      void get_key(size_t id, key_t *k) { memcpy(k, base + id * record_len, key_len); }
      void get_val(size_t id, val_t *v) { memcpy(v, base + id * record_len + key_len, val_len); }
      uint64_t get_pos(key_t k) { return hash_matrix.times(k) & size_mask; }
        
      

      val_t get_key_val(const key_t _key) {
        key_t key;
        if(canonical) {
          key = fasta_parser::reverse_complement(_key, get_mer_len());
          if(key > _key)
            key = _key;
        } else {
          key = _key;
        }
        val_t res = 0;
        if(key == first_key) {
          get_val(0, &res);
          return res;
        }
        if(key == last_key) {
          get_val(last_id - 1, &res);
          return res;
        }
        uint64_t pos = get_pos(key);
        if(pos < first_pos || pos > last_pos)
          return 0;
        uint64_t first = 0, last = last_id;
        while(first < last - 1) {
          uint64_t middle = (first + last) / 2;
          key_t mid_key;
          get_key(middle, &mid_key);
          //          printf("%ld %ld %ld %ld %ld %ld %ld\n", key, pos, first, middle, last, mid_key, get_pos(mid_key));
          if(key == mid_key) {
            get_val(middle, &res);
            return res;
          }
          uint64_t mid_pos = get_pos(mid_key);
          if(mid_pos > pos || (mid_pos == pos && mid_key > key))
            last = middle;
          else
            first = middle;
        }
        return 0;
      }
    };
  }
}
#endif /* __COMPACTED_HASH__ */