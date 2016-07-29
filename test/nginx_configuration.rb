module NginxConfiguration
  def self.default_configuration
    {}
  end

  def self.template_configuration
    File.open(File.expand_path('nginx-test.conf', File.dirname(__FILE__))).read
  end
end
