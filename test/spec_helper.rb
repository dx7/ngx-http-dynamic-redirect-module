require 'rubygems'
# Set up gems listed in the Gemfile.
ENV['BUNDLE_GEMFILE'] ||= File.expand_path('Gemfile', File.dirname(__FILE__))
require 'bundler/setup' if File.exist?(ENV['BUNDLE_GEMFILE'])
Bundler.require(:default, :test) if defined?(Bundler)

require "net/http"
require "uri"

require File.expand_path('nginx_configuration', File.dirname(__FILE__))

def redis_unix_socket
  File.join(NginxTestHelper::Config.nginx_tests_tmp_dir, "ngx_http_dynamic_redirect_module_test.socket")
end

def redis_host
  'localhost'
end

def redis_port
  63790
end

def redis_db
  4
end

def redis_start
  system("redis-server --port #{redis_port} --unixsocket #{redis_unix_socket} --unixsocketperm 777 --daemonize yes --pidfile #{redis_unix_socket.gsub("socket", "pid")}")
end

def redis_stop
  system("kill `cat #{redis_unix_socket.gsub("socket", "pid")}`")
end

def redis(host=redis_host, port=redis_port, database=redis_db)
  @redis ||= Redis.new(host: host, port: port, db: database, driver: :hiredis) rescue nil
end

def redis_clear
  redis.flushdb
end

def insert_entry_on_redis(orig, dest)
  redis.set(orig, dest)
end

def response_for(url)
  uri = URI.parse(url)
  Net::HTTP.get_response(uri)
end

def log_changes_for(log_file, &block)
  log_pre = File.readlines(log_file)
  block.call
  sleep(0.5) if NginxTestHelper.nginx_executable.include?("valgrind")
  log_pos = File.readlines(log_file)
  (log_pos - log_pre).join
end

RSpec.configure do |config|
  config.before(:suite) do
    FileUtils.mkdir_p NginxTestHelper.nginx_tests_tmp_dir
    redis_start
  end

  config.after(:suite) do
    redis_stop
  end

  config.before(:each) do
    redis_clear
  end

  config.after(:each) do
    NginxTestHelper::Config.delete_config_and_log_files(config_id) if has_passed?
    redis.quit
    @redis = nil
  end

  config.order = "random"
  config.run_all_when_everything_filtered = true
end
