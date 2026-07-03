/*
   Copyright (c) 2026, MariaDB Corporation
   Copyright (c) 2026, lefred

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA */

#define MYSQL_SERVER

#include <my_global.h>
#include <sql_class.h>
#include <mysql/plugin_data_type.h>
#include "tztime.h"
#include "sql_time.h"
#include "sql_type.h"


class Field_datetime_tzf;


static const uint DATETIME_TZ_OFFSET_LENGTH= 6;


class Type_handler_datetime_tz: public Type_handler_timestamp2
{
public:
  const Type_collection *type_collection() const override;
  Field *make_table_field(MEM_ROOT *root,
                          const LEX_CSTRING *name,
                          const Record_addr &addr,
                          const Type_all_attributes &attr,
                          TABLE_SHARE *share) const override;
  Field *make_table_field_from_def(TABLE_SHARE *share, MEM_ROOT *mem_root,
                                   const LEX_CSTRING *name,
                                   const Record_addr &rec,
                                   const Bit_addr &bit,
                                   const Column_definition_attributes *attr,
                                   uint32 flags) const override;
  const Type_handler *type_handler_for_implicit_upgrade() const override
  {
    return this;
  }
  void Column_definition_implicit_upgrade_to_this(Column_definition *old)
                                                           const override
  {
  }
  int Item_save_in_field(Item *item, Field *field, bool no_conversions)
                         const override;
};


static Type_handler_datetime_tz type_handler_datetime_tz;


class Type_collection_datetime_tz: public Type_collection
{
protected:
  const Type_handler *aggregate_common(const Type_handler *h1,
                                       const Type_handler *h2) const;
public:
  const Type_handler *aggregate_for_result(const Type_handler *h1,
                                           const Type_handler *h2)
                                           const override
  {
    return aggregate_common(h1, h2);
  }
  const Type_handler *aggregate_for_comparison(const Type_handler *h1,
                                               const Type_handler *h2)
                                               const override
  {
    return aggregate_common(h1, h2);
  }
  const Type_handler *aggregate_for_min_max(const Type_handler *h1,
                                            const Type_handler *h2)
                                            const override
  {
    return aggregate_common(h1, h2);
  }
  const Type_handler *aggregate_for_num_op(const Type_handler *h1,
                                           const Type_handler *h2)
                                           const override
  {
    return aggregate_common(h1, h2);
  }
};


static Type_collection_datetime_tz type_collection_datetime_tz;


class Field_datetime_tzf: public Field_timestampf
{
  static bool parse_two_digits(const char *str, uint *to)
  {
    if (!my_isdigit(&my_charset_latin1, str[0]) ||
        !my_isdigit(&my_charset_latin1, str[1]))
      return true;
    *to= (uint) ((str[0] - '0') * 10 + str[1] - '0');
    return false;
  }

  static bool parse_time_zone_offset(const char *str, size_t length,
                                     size_t *datetime_length,
                                     long *offset_seconds)
  {
    const char *end= str + length;
    while (end > str && my_isspace(&my_charset_latin1, end[-1]))
      end--;

    const char *pos= end;
    if (pos - str >= 6 && (pos[-6] == '+' || pos[-6] == '-') &&
        my_isdigit(&my_charset_latin1, pos[-5]) &&
        my_isdigit(&my_charset_latin1, pos[-4]) &&
        pos[-3] == ':' &&
        my_isdigit(&my_charset_latin1, pos[-2]) &&
        my_isdigit(&my_charset_latin1, pos[-1]))
      pos-= 6;
    else if (pos - str >= 3 && (pos[-3] == '+' || pos[-3] == '-') &&
             my_isdigit(&my_charset_latin1, pos[-2]) &&
             my_isdigit(&my_charset_latin1, pos[-1]))
      pos-= 3;
    else
      return false;

    uint hours, minutes= 0;
    if (parse_two_digits(pos + 1, &hours))
      return true;
    if (end - pos == 6 && parse_two_digits(pos + 4, &minutes))
      return true;
    if (hours > 13 || minutes > 59 || (hours == 13 && minutes > 59))
      return true;

    *datetime_length= (size_t) (pos - str);
    *offset_seconds= (long) (hours * 3600 + minutes * 60);
    if (*pos == '-')
      *offset_seconds= -*offset_seconds;
    return false;
  }

  int store_temporal_string(const char *from, size_t len, CHARSET_INFO *cs)
  {
    THD *thd= get_thd();
    size_t datetime_length= len;
    long offset_seconds= 0;
    bool has_offset= false;

    if (parse_time_zone_offset(from, len, &datetime_length, &offset_seconds))
    {
      ErrConvString str(from, len, cs);
      set_datetime_warning(WARN_DATA_TRUNCATED, &str, "datetime", 1);
      reset();
      return 1;
    }
    has_offset= datetime_length != len;

    MYSQL_TIME_STATUS status;
    my_time_status_init(&status);
    Datetime dt(thd, &status, from, datetime_length, cs,
                Timestamp::DatetimeOptions(thd), dec);
    if (!dt.is_valid_datetime())
    {
      ErrConvString str(from, len, cs);
      set_datetime_warning(WARN_DATA_TRUNCATED, &str, "datetime", 1);
      reset();
      return 1;
    }
    MYSQL_TIME ltime= *dt.get_mysql_time();

    if (!has_offset)
      return Field_timestampf::store_time_dec(&ltime, dec);

    if (!ltime.month)
    {
      reset();
      return zero_time_stored_return_code_with_warning();
    }

    uint conversion_error;
    my_time_t timestamp= my_tz_OFFSET0->TIME_to_gmt_sec(&ltime,
                                                        &conversion_error);
    timestamp-= offset_seconds;
    if (conversion_error ||
        store_timestamp_dec(Timeval(timestamp, ltime.second_part), dec))
    {
      ErrConvString str(from, len, cs);
      set_datetime_warning(ER_WARN_DATA_OUT_OF_RANGE, &str, "datetime", 1);
      reset();
      return 1;
    }
    return status.warnings ? 1 : 0;
  }

  void append_time_zone_offset(String *str, const MYSQL_TIME *ltime) const
  {
    struct my_tz tz_info;
    Time_zone *tz= get_thd()->variables.time_zone;
    tz->get_timezone_information(&tz_info, ltime);

    long minutes= labs(tz_info.seconds_offset) / 60;
    long diff_hr= minutes / 60;
    long diff_min= minutes % 60;

    str->append(tz_info.seconds_offset < 0 ? '-' : '+');
    str->append(static_cast<char>('0' + diff_hr / 10));
    str->append(static_cast<char>('0' + diff_hr % 10));
    str->append(':');
    str->append(static_cast<char>('0' + diff_min / 10));
    str->append(static_cast<char>('0' + diff_min % 10));
  }

public:
  using Field_timestampf::store;

  Field_datetime_tzf(uchar *ptr_arg, uchar *null_ptr_arg,
                     uchar null_bit_arg, enum utype unireg_check_arg,
                     const LEX_CSTRING *field_name_arg,
                     TABLE_SHARE *share,
                     decimal_digits_t dec_arg)
    :Field_timestampf(ptr_arg, null_ptr_arg, null_bit_arg,
                      unireg_check_arg, field_name_arg, share, dec_arg)
  {
    field_length= MAX_DATETIME_WIDTH + DATETIME_TZ_OFFSET_LENGTH +
                  dec_arg + MY_TEST(dec_arg);
  }

  const Type_handler *type_handler() const override;

  bool check_assignability_from(const Type_handler *from,
                                bool ignore) const override
  {
    if (from->cmp_type() == STRING_RESULT ||
        from->cmp_type() == TIME_RESULT ||
        from->can_return_date())
      return false;
    return Field_timestampf::check_assignability_from(from, ignore);
  }

  int store(const char *from, size_t len, CHARSET_INFO *cs) override
  {
    return store_temporal_string(from, len, cs);
  }

  void sql_type(String &str) const override
  {
    sql_type_opt_dec_comment(str, type_handler()->name(), dec,
                             type_version_mysql56());
  }

  enum_conv_type rpl_conv_type_from(const Conv_source &source,
                                    const Relay_log_info *rli,
                                    const Conv_param &param) const override
  {
    return Field_timestampf::rpl_conv_type_from(source, rli, param);
  }

  uint32 pack_length() const override
  {
    return my_timestamp_binary_length(dec);
  }

  uint row_pack_length() const override
  {
    return pack_length();
  }

  uint pack_length_from_metadata(uint field_metadata) const override
  {
    return my_timestamp_binary_length(field_metadata);
  }

  longlong val_datetime_packed(THD *thd) override
  {
    DBUG_ASSERT(marked_for_read());
    MYSQL_TIME ltime;
    get_date(&ltime, date_mode_t(0));
    return pack_time(&ltime);
  }

  String *val_str(String *str, String *unused __attribute__((unused)))
    override
  {
    MYSQL_TIME ltime;
    if (get_date(&ltime, Datetime::Options(TIME_NO_ZERO_DATE, get_thd())))
    {
      str->set(STRING_WITH_LEN("0000-00-00 00:00:00"), &my_charset_numeric);
      return str;
    }

    str->alloc(field_length + 1);
    str->length(0);
    char *to= const_cast<char*>(str->ptr());
    uint len= my_datetime_to_str(&ltime, to, dec);
    str->length(len);
    append_time_zone_offset(str, &ltime);
    str->set_charset(&my_charset_numeric);
    return str;
  }

  bool send(Protocol *protocol) override
  {
    return Field::send(protocol);
  }

  uint size_of() const override
  {
    return sizeof *this;
  }

  Binlog_type_info binlog_type_info() const override
  {
    return Binlog_type_info(Field_datetime_tzf::binlog_type(), decimals(), 1);
  }
};


const Type_collection *Type_handler_datetime_tz::type_collection() const
{
  return &type_collection_datetime_tz;
}


Field *Type_handler_datetime_tz::
make_table_field(MEM_ROOT *root, const LEX_CSTRING *name,
                 const Record_addr &addr, const Type_all_attributes &attr,
                 TABLE_SHARE *share) const
{
  return new (root)
    Field_datetime_tzf(addr.ptr(), addr.null_ptr(), addr.null_bit(),
                       Field::NONE, name, share, attr.decimals);
}


Field *Type_handler_datetime_tz::
make_table_field_from_def(TABLE_SHARE *share, MEM_ROOT *mem_root,
                          const LEX_CSTRING *name,
                          const Record_addr &rec,
                          const Bit_addr &bit,
                          const Column_definition_attributes *attr,
                          uint32 flags) const
{
  DBUG_ASSERT(attr->decimals == attr->temporal_dec(MAX_DATETIME_WIDTH));
  return new (mem_root)
    Field_datetime_tzf(rec.ptr(), rec.null_ptr(), rec.null_bit(),
                       attr->unireg_check, name,
                       share,
                       attr->temporal_dec(MAX_DATETIME_WIDTH));
}


const Type_handler *Field_datetime_tzf::type_handler() const
{
  return &type_handler_datetime_tz;
}


int Type_handler_datetime_tz::
Item_save_in_field(Item *item, Field *field, bool no_conversions) const
{
  if (item->cmp_type() == STRING_RESULT)
  {
    StringBuffer<MAX_FIELD_WIDTH> tmp;
    String *res= item->val_str(&tmp);
    if (!res)
      return set_field_to_null_with_conversions(field, no_conversions);
    return field->store(res->ptr(), res->length(), res->charset());
  }
  return Type_handler_timestamp2::Item_save_in_field(item, field,
                                                     no_conversions);
}


const Type_handler *
Type_collection_datetime_tz::aggregate_common(const Type_handler *h1,
                                             const Type_handler *h2) const
{
  if (h1 == h2)
    return h1;

  static const Type_aggregator::Pair agg[]=
  {
    {
      &type_handler_timestamp2,
      &type_handler_datetime_tz,
      &type_handler_datetime_tz
    },
    {NULL, NULL, NULL}
  };

  return Type_aggregator::find_handler_in_array(agg, h1, h2, true);
}


static struct st_mariadb_data_type plugin_descriptor_datetime_with_time_zone=
{
  MariaDB_DATA_TYPE_INTERFACE_VERSION,
  &type_handler_datetime_tz
};


maria_declare_plugin(type_datetime_tz)
{
  MariaDB_DATA_TYPE_PLUGIN,
  &plugin_descriptor_datetime_with_time_zone,
  "datetime_with_time_zone",
  "lefred",
  "Data type DATETIME_WITH_TIME_ZONE",
  PLUGIN_LICENSE_GPL,
  0,
  0,
  0x0100,
  NULL,
  NULL,
  "1.0",
  MariaDB_PLUGIN_MATURITY_BETA
}
maria_declare_plugin_end;
